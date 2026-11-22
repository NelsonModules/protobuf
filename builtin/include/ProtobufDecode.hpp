//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtobufConversion.hpp"
#include "ProtobufRegistry.hpp"
#include "ProtoRecordIndex.hpp"
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
// [value, presence, report] of data decoded with the settings of protobuf.internal.decodeBytes;
// index (null when there is none) finds the first record of 'Records' without reading the
// records before it.
ArrayOfVector
decodeData(int nLhs, const SchemaEntry& entry, const std::string& typeName, const uint8_t* data,
    size_t size, const DecodeSettings& settings, const Protobuf::RecordIndex* index);
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
