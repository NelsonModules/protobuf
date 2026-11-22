//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Byte offsets of the records of a length-delimited file, one every step records, with the
// size and modification time of the file it was built from.
struct RecordIndex
{
    uint64_t fileSize = 0;
    int64_t modified = 0;
    uint64_t records = 0; // complete records
    uint64_t step = 1024;
    std::vector<uint64_t> offsets; // offset of record k * step (0-based)
};
//=============================================================================
// Scans the complete records of data (a cut or invalid tail ends the scan).
RecordIndex
buildRecordIndex(const uint8_t* data, size_t size, uint64_t step);
// Continues the scan of index from position up to limit (a record that starts before limit is
// read whole); true when the scan is over (end of data, or a cut or invalid record).
bool
extendRecordIndex(
    RecordIndex& index, const uint8_t* data, size_t size, size_t& position, size_t limit);
//=============================================================================
// File format: "NPBIDX1\0", then fileSize, modified, records, step, offset count and offsets,
// each as 8 little-endian bytes.
std::string
encodeRecordIndex(const RecordIndex& index);
// False when bytes is not an index.
bool
decodeRecordIndex(const std::string& bytes, RecordIndex& index);
//=============================================================================
// Offset of record (0-based) of data: from the nearest indexed record, then record by record;
// size when the data has fewer records.
size_t
indexedRecordOffset(const RecordIndex& index, const uint8_t* data, size_t size, size_t record);
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
