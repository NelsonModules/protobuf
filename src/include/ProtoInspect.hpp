//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// One field of a message read without schema.
struct RawField
{
    uint32_t number = 0;
    uint32_t wireType = 0; // 0 varint, 1 fixed64, 2 length-delimited, 3 group, 5 fixed32
    uint64_t value = 0; // varint, fixed64 and fixed32 values
    std::string bytes; // length-delimited payload
    // Length-delimited payload that parses as a message, or the fields of a group.
    bool isMessage = false;
    std::vector<RawField> fields;
};
//=============================================================================
// Fields of a message; false when the bytes are not a valid message (all of them must be read).
bool
parseRawMessage(const uint8_t* data, size_t size, std::vector<RawField>& fields, int depth = 0);
//=============================================================================
// Text like protoc --decode_raw: "1: 150", "2: \"text\"", "3 {" ... "}", fixed values in hex.
std::string
rawText(const std::vector<RawField>& fields);
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
