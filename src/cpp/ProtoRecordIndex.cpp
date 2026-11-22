//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoRecordIndex.hpp"
#include "ProtoStream.hpp"
#include "ProtoWire.hpp"
#include <cstring>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    constexpr char kMagic[8] = { 'N', 'P', 'B', 'I', 'D', 'X', '1', '\0' };
    //=============================================================================
    void
    putWord(std::string& out, uint64_t value);
    uint64_t
    getWord(const std::string& bytes, size_t at);
    //=============================================================================
} // namespace
//=============================================================================
RecordIndex
buildRecordIndex(const uint8_t* data, size_t size, uint64_t step)
{
    RecordIndex index;
    index.step = step == 0 ? 1 : step;
    index.fileSize = size;
    size_t position = 0;
    extendRecordIndex(index, data, size, position, size);
    return index;
}
//=============================================================================
bool
extendRecordIndex(
    RecordIndex& index, const uint8_t* data, size_t size, size_t& position, size_t limit)
{
    index.fileSize = size;
    while (position < size) {
        if (position >= limit) {
            return false;
        }
        const uint8_t* p = data + position;
        uint64_t length = 0;
        try {
            length = Wire::readVarint(p, data + size);
        } catch (const ProtoError&) {
            return true;
        }
        const size_t header = (size_t)(p - (data + position));
        if (length > size - position - header) {
            return true;
        }
        if (index.records % index.step == 0) {
            index.offsets.push_back(position);
        }
        index.records++;
        position += header + (size_t)length;
    }
    return true;
}
//=============================================================================
std::string
encodeRecordIndex(const RecordIndex& index)
{
    std::string out(kMagic, sizeof(kMagic));
    putWord(out, index.fileSize);
    putWord(out, (uint64_t)index.modified);
    putWord(out, index.records);
    putWord(out, index.step);
    putWord(out, index.offsets.size());
    for (uint64_t offset : index.offsets) {
        putWord(out, offset);
    }
    return out;
}
//=============================================================================
bool
decodeRecordIndex(const std::string& bytes, RecordIndex& index)
{
    const size_t headerSize = sizeof(kMagic) + 5 * 8;
    if (bytes.size() < headerSize || std::memcmp(bytes.data(), kMagic, sizeof(kMagic)) != 0) {
        return false;
    }
    index.fileSize = getWord(bytes, 8);
    index.modified = (int64_t)getWord(bytes, 16);
    index.records = getWord(bytes, 24);
    index.step = getWord(bytes, 32);
    const uint64_t count = getWord(bytes, 40);
    if (index.step == 0 || count > (bytes.size() - headerSize) / 8
        || bytes.size() != headerSize + count * 8
        || count != (index.records + index.step - 1) / index.step) {
        return false;
    }
    index.offsets.resize((size_t)count);
    for (size_t k = 0; k < count; ++k) {
        index.offsets[k] = getWord(bytes, headerSize + k * 8);
    }
    return true;
}
//=============================================================================
size_t
indexedRecordOffset(const RecordIndex& index, const uint8_t* data, size_t size, size_t record)
{
    if (record >= index.records || index.offsets.empty()) {
        return record == 0 ? 0 : size;
    }
    const size_t slot = (size_t)(record / index.step);
    const size_t start = (size_t)index.offsets[slot];
    if (start > size) {
        return size;
    }
    return start + skipRecords(data + start, size - start, record - slot * (size_t)index.step);
}
//=============================================================================
namespace {
    //=============================================================================
    void
    putWord(std::string& out, uint64_t value)
    {
        char bytes[8];
        for (int k = 0; k < 8; ++k) {
            bytes[k] = (char)((value >> (8 * k)) & 0xFF);
        }
        out.append(bytes, 8);
    }
    //=============================================================================
    uint64_t
    getWord(const std::string& bytes, size_t at)
    {
        uint64_t value = 0;
        for (int k = 7; k >= 0; --k) {
            value = (value << 8) | (uint8_t)bytes[at + (size_t)k];
        }
        return value;
    }
    //=============================================================================
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
