//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoCodec.hpp"
#include <map>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Protobuf text format of one row ("name: value" lines, nested blocks in braces).
std::string
formatText(const Codec& codec, const ColumnSet& set, size_t row);
//=============================================================================
// Appends the message of a text format document as one row ("name: value", nested "name {}").
size_t
parseText(const Codec& codec, ColumnSet& set, const std::string& text);
//=============================================================================
struct JsonOptions
{
    // .proto field names instead of the lowerCamelCase JSON names.
    bool protoNames = false;
    // Two-space indentation and one member per line.
    bool indent = false;
};
//=============================================================================
// Canonical proto3 JSON of one row: default values omitted, 64-bit integers as strings, bytes
// in base64, enums by name, Timestamp/Duration/wrappers/Struct/Value/ListValue/FieldMask as
// their JSON forms.
std::string
formatJson(const Codec& codec, const ColumnSet& set, size_t row, const JsonOptions& options);
//=============================================================================
// Appends the messages of a JSON object (one row) or array of objects (one row each); both
// JSON and .proto field names are accepted. Returns the number of rows added.
size_t
parseJson(const Codec& codec, ColumnSet& set, const std::string& text);
// Appends the messages of NDJSON text: one JSON object per line, blank lines skipped; errors
// name the line. Returns the number of rows added.
size_t
parseJsonLines(const Codec& codec, ColumnSet& set, const std::string& text);
//=============================================================================
// Parsed JSON document, used to compare JSON texts in tests.
struct JsonValue
{
    enum class Type
    {
        Null,
        Bool,
        Number,
        String,
        Array,
        Object
    };
    Type type = Type::Null;
    bool boolean = false;
    double number = 0;
    std::string text; // string value, or the number as written
    std::vector<JsonValue> items;
    std::vector<std::pair<std::string, JsonValue>> members;
};
JsonValue
parseJsonValue(const std::string& text);
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
