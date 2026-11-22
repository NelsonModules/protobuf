//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoCodec.hpp"
#include "ProtoStream.hpp"
#include <cstdint>
#include <memory>
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
// A loaded schema and its codec; the codec refers to the schema of the same entry.
struct SchemaEntry
{
    Protobuf::Schema schema;
    std::unique_ptr<Protobuf::Codec> codec;
    std::string path;
};
//=============================================================================
uint64_t
registerSchema(Protobuf::Schema&& schema, const std::string& path);
//=============================================================================
// Raises a Nelson error when the id is unknown.
std::shared_ptr<const SchemaEntry>
requireSchema(double id);
//=============================================================================
bool
releaseSchema(double id);
//=============================================================================
void
releaseAllSchemas();
//=============================================================================
// A stream decoder; it keeps its schema alive while it exists.
struct DecoderEntry
{
    std::shared_ptr<const SchemaEntry> schema;
    std::unique_ptr<Protobuf::StreamDecoder> decoder;
    // 'Where' filter: rows given to the condition, and the rows it kept since the last read.
    bool filtering = false;
    Protobuf::ColumnSet held;
    Protobuf::ColumnSet kept;
};
//=============================================================================
uint64_t
registerDecoder(std::shared_ptr<const SchemaEntry> schema, int32_t messageIndex, bool skipInvalid);
//=============================================================================
// Raises a Nelson error when the id is unknown.
std::shared_ptr<DecoderEntry>
requireDecoder(double id);
//=============================================================================
bool
releaseDecoder(double id);
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
