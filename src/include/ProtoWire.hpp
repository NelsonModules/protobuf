//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
// Internal: inline readers of the protobuf wire format, used by the codec hot loop.
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoSchema.hpp"
#include <cstring>
#include <vector>
//=============================================================================
#if defined(_MSC_VER)
#define PROTO_INLINE __forceinline
#else
#define PROTO_INLINE inline __attribute__((always_inline))
#endif
#if defined(_WIN32) || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
#define PROTO_LITTLE_ENDIAN 1
#else
#define PROTO_LITTLE_ENDIAN 0
#endif
//=============================================================================
namespace Nelson::Protobuf::Wire {
//=============================================================================
constexpr uint32_t kVarint = 0;
constexpr uint32_t kFixed64 = 1;
constexpr uint32_t kLength = 2;
constexpr uint32_t kStartGroup = 3;
constexpr uint32_t kEndGroup = 4;
constexpr uint32_t kFixed32 = 5;
//=============================================================================
// Multi-byte varint, with or without room for the longest encoding.
uint64_t
readVarintSlow(const uint8_t*& p, const uint8_t* end);
//=============================================================================
PROTO_INLINE uint64_t
readVarint(const uint8_t*& p, const uint8_t* end)
{
    if (p < end && *p < 0x80) {
        return *p++;
    }
    return readVarintSlow(p, end);
}
//=============================================================================
PROTO_INLINE size_t
readLength(const uint8_t*& p, const uint8_t* end)
{
    const uint64_t length = readVarint(p, end);
    if (length > (uint64_t)(end - p)) {
        Error("protobuf:decodeError:truncatedLengthDelimitedFieldInput",
            "truncated length-delimited field in the input.");
    }
    return (size_t)length;
}
//=============================================================================
PROTO_INLINE uint32_t
readFixed32(const uint8_t*& p, const uint8_t* end)
{
    if (end - p < 4) {
        Error("protobuf:decodeError:truncatedFixed32ValueInput",
            "truncated fixed32 value in the input.");
    }
    const uint32_t value
        = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4;
    return value;
}
//=============================================================================
PROTO_INLINE uint64_t
readFixed64(const uint8_t*& p, const uint8_t* end)
{
    if (end - p < 8) {
        Error("protobuf:decodeError:truncatedFixed64ValueInput",
            "truncated fixed64 value in the input.");
    }
    const uint64_t low
        = (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24);
    const uint64_t high
        = (uint64_t)p[4] | ((uint64_t)p[5] << 8) | ((uint64_t)p[6] << 16) | ((uint64_t)p[7] << 24);
    p += 8;
    return low | (high << 32);
}
//=============================================================================
template <typename T>
inline void
store(uint8_t* destination, T value)
{
    std::memcpy(destination, &value, sizeof(T));
}
//=============================================================================
// Reads one value of a scalar field into its column slot (natural C type of the value kind).
PROTO_INLINE void
readScalar(FieldType type, uint8_t* destination, const uint8_t*& p, const uint8_t* end)
{
    switch (type) {
    case FieldType::Double:
    case FieldType::Fixed64:
    case FieldType::SFixed64:
        if (PROTO_LITTLE_ENDIAN && end - p >= 8) {
            std::memcpy(destination, p, 8);
            p += 8;
        } else {
            store<uint64_t>(destination, readFixed64(p, end));
        }
        break;
    case FieldType::Float:
    case FieldType::Fixed32:
    case FieldType::SFixed32:
        if (PROTO_LITTLE_ENDIAN && end - p >= 4) {
            std::memcpy(destination, p, 4);
            p += 4;
        } else {
            store<uint32_t>(destination, readFixed32(p, end));
        }
        break;
    case FieldType::Int64:
    case FieldType::UInt64:
        store<uint64_t>(destination, readVarint(p, end));
        break;
    case FieldType::Int32:
    case FieldType::Enum:
    case FieldType::UInt32:
        store<uint32_t>(destination, (uint32_t)readVarint(p, end));
        break;
    case FieldType::Bool:
        *destination = (uint8_t)(readVarint(p, end) != 0);
        break;
    case FieldType::SInt32: {
        const uint32_t n = (uint32_t)readVarint(p, end);
        store<uint32_t>(destination, (n >> 1) ^ (~(n & 1) + 1));
    } break;
    case FieldType::SInt64: {
        const uint64_t n = readVarint(p, end);
        store<uint64_t>(destination, (n >> 1) ^ (~(n & 1) + 1));
    } break;
    default:
        Error("protobuf:decodeError:unexpectedScalarType", "unexpected scalar type.");
    }
}
//=============================================================================
// Appends the values of a packed field; fixed-size values are copied in one block.
inline void
readPacked(FieldType type, uint32_t wireType, uint32_t width, std::vector<uint8_t>& values,
    const uint8_t*& p, const uint8_t* stop)
{
    const size_t length = (size_t)(stop - p);
    if (PROTO_LITTLE_ENDIAN && (wireType == kFixed32 || wireType == kFixed64)) {
        if (length % width != 0) {
            if (wireType == kFixed32) {
                Error("protobuf:decodeError:truncatedFixed32ValueInput",
                    "truncated fixed32 value in the input.");
            }
            Error("protobuf:decodeError:truncatedFixed64ValueInput",
                "truncated fixed64 value in the input.");
        }
        const size_t n = values.size();
        values.resize(n + length);
        std::memcpy(values.data() + n, p, length);
        p = stop;
        return;
    }
    while (p < stop) {
        const size_t n = values.size();
        values.resize(n + width);
        readScalar(type, values.data() + n, p, stop);
    }
}
//=============================================================================
} // namespace Nelson::Protobuf::Wire
//=============================================================================
