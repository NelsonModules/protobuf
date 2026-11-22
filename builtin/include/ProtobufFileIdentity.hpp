//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoRecordIndex.hpp"
#include <string>
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
// Last modification time of a file (file system clock ticks), 0 when unknown.
int64_t
fileModificationTime(const std::string& utf8Path);
//=============================================================================
// Reads indexPath into index; false when it is missing, unreadable, or was built for another
// version of the file (size or modification time).
bool
readValidIndex(const std::string& indexPath, const std::string& filePath, size_t fileSize,
    Protobuf::RecordIndex& index);
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
