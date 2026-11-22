//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ArrayOf.hpp"
#include "ProtoCodec.hpp"
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
// One struct element per decoded row (1xN struct array).
ArrayOf
rowsToStructArray(const Protobuf::Codec& codec, const Protobuf::ColumnSet& set,
    bool wellKnownTypes = true, bool fillDefaults = false, bool enumsAsText = false);
//=============================================================================
// One Nx1 column per field; presence gets logical columns for fields that can be missing.
ArrayOf
rowsToColumns(const Protobuf::Codec& codec, const Protobuf::ColumnSet& set, bool missingAsNaN,
    bool wellKnownTypes, ArrayOf& presence, bool fillDefaults = false, bool enumsAsText = false);
//=============================================================================
// Append the rows of a struct of Nx1 columns (the 'columns' layout); presence is the struct
// of logical columns returned by decoding, or anything else when every value is present.
void
appendColumns(const Protobuf::Codec& codec, Protobuf::ColumnSet& set, const ArrayOf& columns,
    const ArrayOf& presence);
//=============================================================================
// Append the elements of a struct array as rows.
void
appendStructRows(const Protobuf::Codec& codec, Protobuf::ColumnSet& set, const ArrayOf& value);
//=============================================================================
// Report struct of a decoding: Type, Records, Skipped, and PendingBytes when pending >= 0.
ArrayOf
decodeReport(const std::string& typeName, size_t records, size_t skipped, int64_t pending = -1);
//=============================================================================
// Decoding options, as the struct built by protobuf.internal.decodeBytes.
struct DecodeSettings
{
    bool delimited = false;
    bool columns = false;
    bool skipInvalid = false;
    bool missingAsNaN = false;
    bool wellKnownTypes = true;
    // Absent fields read as their default value (the presence output still tells them).
    bool fillDefaults = false;
    // Enum values as their names (the number as text when it has no name).
    bool enumsAsText = false;
    bool keepUnknown = false;
    ArrayOf fields;
    ArrayOf records;
    size_t workers = 0;
};
DecodeSettings
decodeSettings(const ArrayOf& options);
//=============================================================================
// Rows in the layout of the settings (struct array or columns with presence).
ArrayOf
rowsToValue(const Protobuf::Codec& codec, const Protobuf::ColumnSet& set,
    const DecodeSettings& settings, ArrayOf& presence);
//=============================================================================
// Selection of a 'Fields' option (names as cell or string array, "a.b" for a field of a
// sub-message); null when names is empty (every field).
std::shared_ptr<const Protobuf::FieldSelection>
fieldSelection(const Protobuf::Schema& schema, int32_t messageIndex, const ArrayOf& names);
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
