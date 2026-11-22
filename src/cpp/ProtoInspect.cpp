//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoInspect.hpp"
#include <cstdio>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    constexpr int kMaxRawDepth = 100;
    //=============================================================================
    bool
    rawVarint(const uint8_t*& p, const uint8_t* end, uint64_t& value);
    bool
    parseFields(const uint8_t*& p, const uint8_t* end, std::vector<RawField>& fields, int depth,
        uint32_t group);
    void
    appendText(std::string& out, const std::vector<RawField>& fields, int indent);
    std::string
    cEscape(const std::string& bytes);
    //=============================================================================
} // namespace
//=============================================================================
bool
parseRawMessage(const uint8_t* data, size_t size, std::vector<RawField>& fields, int depth)
{
    const uint8_t* p = data;
    fields.clear();
    return parseFields(p, data + size, fields, depth, 0) && p == data + size;
}
//=============================================================================
std::string
rawText(const std::vector<RawField>& fields)
{
    std::string out;
    appendText(out, fields, 0);
    return out;
}
//=============================================================================
namespace {
    //=============================================================================
    bool
    rawVarint(const uint8_t*& p, const uint8_t* end, uint64_t& value)
    {
        value = 0;
        for (int shift = 0; shift < 70; shift += 7) {
            if (p >= end) {
                return false;
            }
            const uint8_t byte = *p++;
            value |= (uint64_t)(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return true;
            }
        }
        return false;
    }
    //=============================================================================
    // Reads fields until the end, or until the end-group tag of group (non zero).
    bool
    parseFields(const uint8_t*& p, const uint8_t* end, std::vector<RawField>& fields, int depth,
        uint32_t group)
    {
        if (depth > kMaxRawDepth) {
            return false;
        }
        while (p < end) {
            // Tags are read as 32-bit varints: upper bits of a longer varint are dropped.
            uint64_t wide = 0;
            if (!rawVarint(p, end, wide)) {
                return false;
            }
            const uint32_t tag = (uint32_t)wide;
            if ((tag >> 3) == 0) {
                return false;
            }
            RawField field;
            field.number = tag >> 3;
            field.wireType = tag & 7;
            switch (field.wireType) {
            case 0:
                if (!rawVarint(p, end, field.value)) {
                    return false;
                }
                break;
            case 1:
            case 5: {
                const size_t width = field.wireType == 1 ? 8 : 4;
                if ((size_t)(end - p) < width) {
                    return false;
                }
                for (size_t k = 0; k < width; ++k) {
                    field.value |= (uint64_t)p[k] << (8 * k);
                }
                p += width;
            } break;
            case 2: {
                uint64_t length = 0;
                if (!rawVarint(p, end, length) || length > (uint64_t)(end - p)) {
                    return false;
                }
                field.bytes.assign(reinterpret_cast<const char*>(p), (size_t)length);
                // A non-empty payload that reads entirely as fields is shown as a message.
                if (length > 0) {
                    std::vector<RawField> inner;
                    if (parseRawMessage(p, (size_t)length, inner, depth + 1)) {
                        field.isMessage = true;
                        field.fields = std::move(inner);
                    }
                }
                p += length;
            } break;
            case 3:
                field.isMessage = true;
                if (!parseFields(p, end, field.fields, depth + 1, field.number)) {
                    return false;
                }
                break;
            case 4:
                return group != 0 && field.number == group;
            default:
                return false;
            }
            fields.push_back(std::move(field));
        }
        return group == 0;
    }
    //=============================================================================
    void
    appendText(std::string& out, const std::vector<RawField>& fields, int indent)
    {
        const std::string pad((size_t)indent, ' ');
        for (const RawField& field : fields) {
            const std::string number = std::to_string(field.number);
            char buffer[32];
            switch (field.wireType) {
            case 0:
                out += pad + number + ": " + std::to_string(field.value) + "\n";
                break;
            case 1:
                std::snprintf(buffer, sizeof(buffer), "0x%016llx", (unsigned long long)field.value);
                out += pad + number + ": " + buffer + "\n";
                break;
            case 5:
                std::snprintf(buffer, sizeof(buffer), "0x%08x", (unsigned)field.value);
                out += pad + number + ": " + buffer + "\n";
                break;
            default:
                if (field.isMessage) {
                    out += pad + number + " {\n";
                    appendText(out, field.fields, indent + 2);
                    out += pad + "}\n";
                } else {
                    out += pad + number + ": \"" + cEscape(field.bytes) + "\"\n";
                }
            }
        }
    }
    //=============================================================================
    std::string
    cEscape(const std::string& bytes)
    {
        std::string out;
        for (unsigned char c : bytes) {
            switch (c) {
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\'':
                out += "\\'";
                break;
            case '\\':
                out += "\\\\";
                break;
            default:
                if (c < 0x20 || c >= 0x7F) {
                    char octal[8];
                    std::snprintf(octal, sizeof(octal), "\\%03o", c);
                    out += octal;
                } else {
                    out.push_back((char)c);
                }
            }
        }
        return out;
    }
    //=============================================================================
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
