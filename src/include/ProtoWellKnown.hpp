//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include <string>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Built-in .proto text of a well-known type file ("google/protobuf/timestamp.proto"...),
// nullptr for other names. Same messages and field numbers as the reference definitions.
const char*
wellKnownProtoText(const std::string& name);
// google/protobuf/descriptor.proto of the reference compiler (custom option definitions).
const char*
descriptorProtoText();
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
