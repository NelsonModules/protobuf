//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoMappedFile.hpp"
#include "ProtoSchema.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define PROTO_WITH_MMAP 1
#endif
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    // Whole file read into memory (used when mapping is not possible).
    std::string
    readWholeFile(const std::string& utf8Path);
    //=============================================================================
} // namespace
//=============================================================================
MappedFile::MappedFile(const std::string& utf8Path)
{
#if defined(_WIN32)
    const std::wstring path = std::filesystem::u8path(utf8Path).wstring();
    HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        Error("protobuf:fileError:cannotOpen", "cannot open %s.", utf8Path);
    }
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(handle, &fileSize)) {
        CloseHandle(handle);
        Error("protobuf:fileError:cannotReadSize", "cannot read the size of %s.", utf8Path);
    }
    length = (size_t)fileSize.QuadPart;
    if (length > 0) {
        HANDLE section = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        void* address
            = section != nullptr ? MapViewOfFile(section, FILE_MAP_READ, 0, 0, 0) : nullptr;
        if (address != nullptr) {
            file = handle;
            mapping = section;
            view = static_cast<const uint8_t*>(address);
            return;
        }
        if (section != nullptr) {
            CloseHandle(section);
        }
    }
    CloseHandle(handle);
#elif defined(PROTO_WITH_MMAP)
    const int descriptor = ::open(utf8Path.c_str(), O_RDONLY);
    if (descriptor < 0) {
        Error("protobuf:fileError:cannotOpen", "cannot open %s.", utf8Path);
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0) {
        ::close(descriptor);
        Error("protobuf:fileError:cannotReadSize", "cannot read the size of %s.", utf8Path);
    }
    length = (size_t)status.st_size;
    if (length > 0) {
        void* address = mmap(nullptr, length, PROT_READ, MAP_PRIVATE, descriptor, 0);
        if (address != MAP_FAILED) {
            ::close(descriptor);
            mapping = address;
            view = static_cast<const uint8_t*>(address);
            return;
        }
    }
    ::close(descriptor);
#endif
    copy = readWholeFile(utf8Path);
    length = copy.size();
    view = reinterpret_cast<const uint8_t*>(copy.data());
}
//=============================================================================
MappedFile::~MappedFile()
{
#if defined(_WIN32)
    if (mapping != nullptr) {
        UnmapViewOfFile(view);
        CloseHandle(static_cast<HANDLE>(mapping));
        CloseHandle(static_cast<HANDLE>(file));
    }
#elif defined(PROTO_WITH_MMAP)
    if (mapping != nullptr) {
        munmap(mapping, length);
    }
#endif
}
//=============================================================================
namespace {
    //=============================================================================
    std::string
    readWholeFile(const std::string& utf8Path)
    {
        std::ifstream stream(std::filesystem::u8path(utf8Path), std::ios::binary);
        if (!stream) {
            Error("protobuf:fileError:cannotOpen", "cannot open %s.", utf8Path);
        }
        std::ostringstream content;
        content << stream.rdbuf();
        return content.str();
    }
    //=============================================================================
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
