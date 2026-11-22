//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoStream.hpp"
#include <algorithm>
#include <exception>
#if !defined(__EMSCRIPTEN__) || defined(__EMSCRIPTEN_PTHREADS__)
#define PROTO_WITH_THREADS 1
#include <thread>
#else
#define PROTO_WITH_THREADS 0
#endif
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    constexpr size_t kMaxVarintBytes = 10;
    //=============================================================================
    // Length of a record header (varint) and its message size; false when the varint is cut.
    bool
    readHeader(const uint8_t* data, size_t size, size_t& header, uint64_t& length);
    //=============================================================================
} // namespace
//=============================================================================
StreamDecoder::StreamDecoder(const Codec& codecRef, int32_t messageIndex, bool skip)
    : codec(codecRef), skipInvalid(skip), set(codecRef.createColumnSet(messageIndex))
{
}
//=============================================================================
StreamResult
StreamDecoder::push(const uint8_t* data, size_t size)
{
    StreamResult total;
    size_t used = 0;
    if (!pending.empty()) {
        // Complete the cut record with the first bytes of data only.
        size_t header = 0;
        uint64_t length = 0;
        while (!readHeader(
                   reinterpret_cast<const uint8_t*>(pending.data()), pending.size(), header, length)
            && used < size) {
            pending.push_back((char)data[used++]);
        }
        if (!readHeader(
                reinterpret_cast<const uint8_t*>(pending.data()), pending.size(), header, length)) {
            return total;
        }
        const size_t missing = (size_t)std::min<uint64_t>(
            header + length - std::min<uint64_t>(header + length, pending.size()), size - used);
        pending.append(reinterpret_cast<const char*>(data + used), missing);
        used += missing;
        if (pending.size() < header + length) {
            return total;
        }
        std::string record;
        record.swap(pending);
        decodeComplete(reinterpret_cast<const uint8_t*>(record.data()), record.size(), total);
    }
    const uint8_t* rest = data + used;
    const size_t restSize = size - used;
    const size_t complete = completeRecordsPrefix(rest, restSize);
    decodeComplete(rest, complete, total);
    pending.assign(reinterpret_cast<const char*>(rest + complete), restSize - complete);
    return total;
}
//=============================================================================
void
StreamDecoder::decodeComplete(const uint8_t* data, size_t size, StreamResult& total)
{
    if (size == 0) {
        return;
    }
    StreamResult result = codec.appendDelimited(set, data, size, skipInvalid, seen);
    seen += result.records + result.skipped;
    total.records += result.records;
    total.skipped += result.skipped;
}
//=============================================================================
ColumnSet
StreamDecoder::take()
{
    ColumnSet rows = codec.emptyLike(set);
    std::swap(rows, set);
    return rows;
}
//=============================================================================
size_t
completeRecordsPrefix(const uint8_t* data, size_t size)
{
    size_t position = 0;
    while (position < size) {
        size_t header = 0;
        uint64_t length = 0;
        if (!readHeader(data + position, size - position, header, length)
            || length > size - position - header) {
            break;
        }
        position += header + (size_t)length;
    }
    return position;
}
//=============================================================================
size_t
skipRecords(const uint8_t* data, size_t size, size_t count)
{
    size_t position = 0;
    for (size_t k = 0; k < count && position < size; ++k) {
        size_t header = 0;
        uint64_t length = 0;
        if (!readHeader(data + position, size - position, header, length)
            || length > size - position - header) {
            Error("protobuf:decodeError:atRecord", "record %d: %s", k + 1,
                Message("protobuf:decodeError:truncatedLengthDelimitedFieldInput",
                    "truncated length-delimited field in the input."));
        }
        position += header + (size_t)length;
    }
    return position;
}
//=============================================================================
std::string
encodeDelimitedParallel(const Codec& codec, const ColumnSet& set, size_t workers)
{
    constexpr size_t kRowsPerWorker = 100000;
    constexpr size_t kMaxWorkers = 16;
    size_t count = workers;
#if PROTO_WITH_THREADS
    if (count == 0) {
        const size_t cores = std::max<size_t>(1, std::thread::hardware_concurrency());
        count = std::min({ cores, kMaxWorkers, set.rows / kRowsPerWorker });
    }
#else
    count = 1;
#endif
    count = std::min(count, set.rows);
    if (count <= 1) {
        return codec.encodeDelimited(set);
    }
#if PROTO_WITH_THREADS
    std::vector<std::string> pieces(count);
    std::vector<std::exception_ptr> errors(count);
    std::vector<std::thread> threads;
    for (size_t c = 0; c < count; ++c) {
        threads.emplace_back([&, c]() {
            try {
                codec.encodeDelimitedRows(
                    set, set.rows * c / count, set.rows * (c + 1) / count, pieces[c]);
            } catch (...) {
                errors[c] = std::current_exception();
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    size_t total = 0;
    for (size_t c = 0; c < count; ++c) {
        if (errors[c]) {
            std::rethrow_exception(errors[c]);
        }
        total += pieces[c].size();
    }
    std::string out = std::move(pieces[0]);
    out.reserve(total);
    for (size_t c = 1; c < count; ++c) {
        out += pieces[c];
        std::string().swap(pieces[c]);
    }
    return out;
#else
    return codec.encodeDelimited(set);
#endif
}
//=============================================================================
StreamResult
appendDelimitedParallel(const Codec& codec, ColumnSet& set, const uint8_t* data, size_t size,
    bool skipInvalid, size_t workers, size_t firstRecord)
{
    constexpr size_t kRecordsPerWorker = 200000;
    constexpr size_t kMaxWorkers = 16;
    // Record starts, scanned once; a cut or malformed tail stays with the last piece.
    std::vector<size_t> starts;
    size_t position = 0;
    try {
        while (position < size) {
            size_t header = 0;
            uint64_t length = 0;
            if (!readHeader(data + position, size - position, header, length)
                || length > size - position - header) {
                break;
            }
            starts.push_back(position);
            position += header + (size_t)length;
        }
    } catch (const ProtoError&) {
    }
    size_t count = workers;
#if PROTO_WITH_THREADS
    if (count == 0) {
        const size_t cores = std::max<size_t>(1, std::thread::hardware_concurrency());
        count = std::min({ cores, kMaxWorkers, starts.size() / kRecordsPerWorker });
    }
#else
    count = 1;
#endif
    count = std::min(count, starts.size());
    if (count <= 1) {
        return codec.appendDelimited(set, data, size, skipInvalid, firstRecord);
    }
#if PROTO_WITH_THREADS
    std::vector<ColumnSet> parts;
    std::vector<StreamResult> results(count);
    std::vector<std::exception_ptr> errors(count);
    for (size_t c = 0; c < count; ++c) {
        parts.push_back(codec.emptyLike(set));
    }
    std::vector<std::thread> threads;
    for (size_t c = 0; c < count; ++c) {
        const size_t firstIndex = starts.size() * c / count;
        const size_t lastIndex = starts.size() * (c + 1) / count;
        const size_t begin = starts[firstIndex];
        const size_t end = c + 1 == count ? size : starts[lastIndex];
        threads.emplace_back([&, c, firstIndex, begin, end]() {
            try {
                results[c] = codec.appendDelimited(
                    parts[c], data + begin, end - begin, skipInvalid, firstRecord + firstIndex);
            } catch (...) {
                errors[c] = std::current_exception();
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }
    for (size_t c = 0; c < count; ++c) {
        if (errors[c]) {
            std::rethrow_exception(errors[c]);
        }
    }
    StreamResult total;
    std::vector<ColumnSet*> rest;
    for (size_t c = 0; c < count; ++c) {
        total.records += results[c].records;
        total.skipped += results[c].skipped;
        if (c == 0 && set.rows == 0) {
            // The first piece becomes the result without a copy.
            set = std::move(parts[0]);
        } else {
            rest.push_back(&parts[c]);
        }
    }
    codec.appendRows(set, rest);
    return total;
#else
    return codec.appendDelimited(set, data, size, skipInvalid, firstRecord);
#endif
}
//=============================================================================
namespace {
    //=============================================================================
    bool
    readHeader(const uint8_t* data, size_t size, size_t& header, uint64_t& length)
    {
        length = 0;
        for (size_t k = 0; k < kMaxVarintBytes; ++k) {
            if (k >= size) {
                return false;
            }
            length |= (uint64_t)(data[k] & 0x7F) << (7 * k);
            if ((data[k] & 0x80) == 0) {
                header = k + 1;
                return true;
            }
        }
        Error("protobuf:decodeError:malformedVarintInput", "malformed varint in the input.");
    }
    //=============================================================================
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
