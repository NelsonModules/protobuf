//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoCodec.hpp"
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Decodes a length-delimited stream given in pieces of any size: the bytes of a record cut
// at the end of a piece wait for the next one.
class StreamDecoder
{
public:
    StreamDecoder(const Codec& codec, int32_t messageIndex, bool skipInvalid);
    // Decodes the complete records of pending bytes + data; returns the counts of this call.
    StreamResult
    push(const uint8_t* data, size_t size);
    // Fields to decode (see Codec::select); applies to the next rows.
    void
    setSelection(std::shared_ptr<const FieldSelection> selection)
    {
        codec.select(set, selection);
    }
    // Keep the bytes of unknown fields (see ColumnSet::unknown).
    void
    setKeepUnknown(bool keepUnknown)
    {
        set.keepUnknown = keepUnknown;
    }
    // Rows decoded since the last take(); the decoder continues with no row.
    ColumnSet
    take();
    // Bytes of an incomplete record kept for the next push.
    size_t
    pendingBytes() const
    {
        return pending.size();
    }
    // Records decoded or skipped since the start.
    size_t
    recordsSeen() const
    {
        return seen;
    }
    int32_t
    messageIndex() const
    {
        return set.messageIndex;
    }

private:
    const Codec& codec;
    bool skipInvalid;
    ColumnSet set;
    std::string pending;
    size_t seen = 0;
    void
    decodeComplete(const uint8_t* data, size_t size, StreamResult& total);
};
//=============================================================================
// Size of the longest prefix made of complete records (varint size + message).
size_t
completeRecordsPrefix(const uint8_t* data, size_t size);
//=============================================================================
// Size of the first count records; stops early at the end of the data.
size_t
skipRecords(const uint8_t* data, size_t size, size_t count);
//=============================================================================
// appendDelimited on several threads: the stream is cut at record boundaries, each piece is
// decoded on its own, then the rows are joined in order. workers 0 picks a count from the
// stream size and the processor; builds without threads decode on the calling thread.
StreamResult
appendDelimitedParallel(const Codec& codec, ColumnSet& set, const uint8_t* data, size_t size,
    bool skipInvalid, size_t workers, size_t firstRecord = 0);
//=============================================================================
// encodeDelimited on several threads: each thread encodes a range of rows, then the pieces are
// joined in order (same bytes as one thread). workers 0 picks a count from the row count.
std::string
encodeDelimitedParallel(const Codec& codec, const ColumnSet& set, size_t workers);
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
