//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoSchema.hpp"
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Schema of a serialized FileDescriptorSet (protoc --descriptor_set_out): the last file of the
// set is the loaded one; its imports come from the set, then from the built-in well-known types.
// Each file goes through the .proto checks: proto3 and editions 2023/2024 files are accepted.
Schema
parseDescriptorSet(const std::string& bytes, const std::string& setName);
//=============================================================================
// FileDescriptorSet of a schema: every file, imports before the files importing them.
std::string
encodeDescriptorSet(const Schema& schema);
//=============================================================================
// .proto text of a file of a serialized FileDescriptorProto (used to load descriptor sets).
std::string
descriptorToProtoText(const std::string& fileDescriptorProto, std::string& fileName);
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
