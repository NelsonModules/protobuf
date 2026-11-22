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
// Canonical text of the parsed schema, compared with the reference compiler output.
std::string
dumpDescriptor(const Schema& schema);
//=============================================================================
// Canonical text of one decoded row, exact for every value.
std::string
dumpRow(const Codec& codec, const ColumnSet& set, size_t row);
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
