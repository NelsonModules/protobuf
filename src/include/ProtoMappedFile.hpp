//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include <cstddef>
#include <cstdint>
#include <string>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Read-only view of a whole file: mapped in memory (pages are read on demand), or read into
// memory where mapping is not available. Throws ProtoError when the file cannot be opened.
class MappedFile
{
public:
    explicit MappedFile(const std::string& utf8Path);
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile&
    operator=(const MappedFile&) = delete;
    const uint8_t*
    data() const
    {
        return view;
    }
    size_t
    size() const
    {
        return length;
    }

private:
    const uint8_t* view = nullptr;
    size_t length = 0;
    // Bytes read into memory when the file is empty or cannot be mapped.
    std::string copy;
    void* mapping = nullptr;
    void* file = nullptr;
};
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
