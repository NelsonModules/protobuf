//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtobufFileIdentity.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
int64_t
fileModificationTime(const std::string& utf8Path)
{
    std::error_code error;
    const auto time = std::filesystem::last_write_time(std::filesystem::u8path(utf8Path), error);
    if (error) {
        return 0;
    }
    return (int64_t)time.time_since_epoch().count();
}
//=============================================================================
bool
readValidIndex(const std::string& indexPath, const std::string& filePath, size_t fileSize,
    Protobuf::RecordIndex& index)
{
    std::ifstream in(std::filesystem::u8path(indexPath), std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream bytes;
    bytes << in.rdbuf();
    return Protobuf::decodeRecordIndex(bytes.str(), index) && index.fileSize == fileSize
        && index.modified == fileModificationTime(filePath);
}
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
