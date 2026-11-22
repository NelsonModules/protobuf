//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoDump.hpp"
#include <algorithm>
#include <cinttypes>
#include <cstdio>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    void
    dumpMessageDef(std::string& out, const Schema& schema, int32_t messageIndex, int indent);
    void
    dumpEnumDef(std::string& out, const Schema& schema, const EnumDef& enumDef, int indent);
    // "field" or "extension" line, then the resolved line outside proto3 files.
    void
    dumpField(std::string& out, const Schema& schema, int32_t file, const FieldDef& f,
        const std::string& pad, bool extension);
    // Name of a field in a row dump: [full.name] for an extension.
    std::string
    rowName(const FieldDef& field);
    void
    dumpMessageRow(
        std::string& out, const Schema& schema, const ColumnSet& set, size_t row, int indent);
    std::string
    scalarText(const FieldDef& field, const Column& column, size_t index);
    std::string
    hexBytes(const std::string& bytes);
    std::vector<int32_t>
    fieldsByNumber(const MessageDef& message);
    //=============================================================================
} // namespace
//=============================================================================
std::string
dumpDescriptor(const Schema& schema)
{
    std::string out;
    // The loaded file only; imported files have their own descriptors.
    for (const FileDef& file : schema.files) {
        if (&file != &schema.files.front()) {
            break;
        }
        out += "package " + file.package + "\n";
        out += "syntax " + file.syntax + "\n";
        if (!file.edition.empty()) {
            out += "edition " + file.edition + "\n";
        }
        for (int32_t m : file.messages) {
            dumpMessageDef(out, schema, m, 0);
        }
        for (int32_t e : file.enums) {
            dumpEnumDef(out, schema, schema.enums[(size_t)e], 0);
        }
        for (int32_t x : file.extensions) {
            dumpField(out, schema, 0, schema.extensions[(size_t)x].field, "", true);
        }
        for (int32_t s : file.services) {
            const ServiceDef& service = schema.services[(size_t)s];
            out += "service " + service.name + "\n";
            for (const MethodDef& method : service.methods) {
                out += "  method " + method.name + " " + method.inputType + " " + method.outputType
                    + " " + (method.clientStreaming ? "1" : "0") + " "
                    + (method.serverStreaming ? "1" : "0") + "\n";
            }
        }
    }
    return out;
}
//=============================================================================
std::string
dumpRow(const Codec& codec, const ColumnSet& set, size_t row)
{
    std::string out;
    dumpMessageRow(out, codec.schema(), set, row, 0);
    return out;
}
//=============================================================================
namespace {
    //=============================================================================
    void
    dumpMessageDef(std::string& out, const Schema& schema, int32_t messageIndex, int indent)
    {
        const MessageDef& message = schema.messages[(size_t)messageIndex];
        const std::string pad((size_t)indent, ' ');
        out += pad + "message " + message.name + "\n";
        for (const FieldDef& f : message.fields) {
            if (f.extension.empty()) {
                dumpField(out, schema, message.file, f, pad + "  ", false);
            }
        }
        for (int32_t x : message.extensions) {
            dumpField(
                out, schema, message.file, schema.extensions[(size_t)x].field, pad + "  ", true);
        }
        for (int32_t nested : message.nestedMessages) {
            dumpMessageDef(out, schema, nested, indent + 2);
        }
        for (int32_t nested : message.nestedEnums) {
            dumpEnumDef(out, schema, schema.enums[(size_t)nested], indent + 2);
        }
        for (const OneofDef& oneof : message.oneofs) {
            out += pad + "  oneof " + oneof.name + "\n";
        }
        for (const ReservedRange& range : message.reservedRanges) {
            out += pad + "  reserved_range " + std::to_string(range.start) + " "
                + std::to_string(range.end) + "\n";
        }
        for (const std::string& name : message.reservedNames) {
            out += pad + "  reserved_name " + name + "\n";
        }
        for (const ReservedRange& range : message.extensionRanges) {
            out += pad + "  extension_range " + std::to_string(range.start) + " "
                + std::to_string(range.end) + "\n";
        }
        if (message.mapEntry) {
            out += pad + "  map_entry\n";
        }
    }
    //=============================================================================
    void
    dumpEnumDef(std::string& out, const Schema& schema, const EnumDef& enumDef, int indent)
    {
        const std::string pad((size_t)indent, ' ');
        out += pad + "enum " + enumDef.name + "\n";
        if (schema.files[(size_t)enumDef.file].syntax != "proto3" && enumDef.closed) {
            out += pad + "  closed\n";
        }
        for (const EnumValueDef& value : enumDef.values) {
            out += pad + "  value " + value.name + " " + std::to_string(value.number) + "\n";
        }
        for (const ReservedRange& range : enumDef.reservedRanges) {
            out += pad + "  reserved_range " + std::to_string(range.start) + " "
                + std::to_string(range.end) + "\n";
        }
        for (const std::string& name : enumDef.reservedNames) {
            out += pad + "  reserved_name " + name + "\n";
        }
    }
    //=============================================================================
    void
    dumpMessageRow(
        std::string& out, const Schema& schema, const ColumnSet& set, size_t row, int indent)
    {
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        const std::string pad((size_t)indent, ' ');
        for (int32_t fieldIndex : fieldsByNumber(message)) {
            const FieldDef& field = message.fields[(size_t)fieldIndex];
            const Column& column = set.columns[(size_t)fieldIndex];
            if (column.repeated) {
                size_t first = (size_t)column.offsets[row];
                size_t last = (size_t)column.offsets[row + 1];
                if (field.isMap) {
                    // Map order is not significant: entries are sorted by their text.
                    std::vector<std::string> entries;
                    for (size_t k = first; k < last; ++k) {
                        std::string entry = pad + rowName(field) + "[] {\n";
                        dumpMessageRow(entry, schema, *column.child, k, indent + 2);
                        entry += pad + "}\n";
                        entries.push_back(entry);
                    }
                    std::sort(entries.begin(), entries.end());
                    for (const std::string& entry : entries) {
                        out += entry;
                    }
                    continue;
                }
                for (size_t k = first; k < last; ++k) {
                    std::string label
                        = pad + rowName(field) + "[" + std::to_string(k - first) + "]";
                    if (column.kind == ValueKind::Message) {
                        out += label + " {\n";
                        dumpMessageRow(out, schema, *column.child, k, indent + 2);
                        out += pad + "}\n";
                    } else {
                        out += label + ": " + scalarText(field, column, k) + "\n";
                    }
                }
                continue;
            }
            if (column.kind == ValueKind::Message) {
                int64_t childRow = column.childRow[row];
                if (childRow >= 0) {
                    out += pad + rowName(field) + " {\n";
                    dumpMessageRow(out, schema, *column.child, (size_t)childRow, indent + 2);
                    out += pad + "}\n";
                }
                continue;
            }
            if (column.hasPresence && column.presence[row] == 0) {
                continue;
            }
            out += pad + rowName(field) + ": " + scalarText(field, column, row) + "\n";
        }
    }
    //=============================================================================
    std::string
    scalarText(const FieldDef& field, const Column& column, size_t index)
    {
        char buffer[64];
        const uint8_t* slot = column.values.data() + index * valueSize(column.kind);
        switch (column.kind) {
        case ValueKind::Double: {
            uint64_t bits;
            std::memcpy(&bits, slot, 8);
            std::snprintf(buffer, sizeof(buffer), "d:%016" PRIx64, bits);
        } break;
        case ValueKind::Float: {
            uint32_t bits;
            std::memcpy(&bits, slot, 4);
            std::snprintf(buffer, sizeof(buffer), "f:%08" PRIx32, bits);
        } break;
        case ValueKind::Int64: {
            int64_t v;
            std::memcpy(&v, slot, 8);
            std::snprintf(buffer, sizeof(buffer), "%" PRId64, v);
        } break;
        case ValueKind::UInt64: {
            uint64_t v;
            std::memcpy(&v, slot, 8);
            std::snprintf(buffer, sizeof(buffer), "%" PRIu64, v);
        } break;
        case ValueKind::Int32: {
            int32_t v;
            std::memcpy(&v, slot, 4);
            std::snprintf(buffer, sizeof(buffer), "%" PRId32, v);
        } break;
        case ValueKind::UInt32: {
            uint32_t v;
            std::memcpy(&v, slot, 4);
            std::snprintf(buffer, sizeof(buffer), "%" PRIu32, v);
        } break;
        case ValueKind::Bool:
            return *slot ? "true" : "false";
        case ValueKind::Bytes:
            return "x:" + hexBytes(column.bytes[index]);
        default:
            return "?";
        }
        (void)field;
        return buffer;
    }
    //=============================================================================
    void
    dumpField(std::string& out, const Schema& schema, int32_t file, const FieldDef& f,
        const std::string& pad, bool extension)
    {
        std::string packed = f.hasPackedOption ? (f.packedOption ? "true" : "false") : "-";
        out += pad + (extension ? "extension " : "field ") + f.name + " " + std::to_string(f.number)
            + " " + std::to_string((int)f.label) + " "
            + std::to_string(f.isGroup ? (int)FieldType::Group : (int)f.type) + " "
            + (f.typeName.empty() ? "-" : f.typeName) + " " + (extension ? f.extendee + " " : "")
            + f.jsonName + " " + (f.oneofIndex >= 0 ? std::to_string(f.oneofIndex) : "-") + " "
            + (f.proto3Optional ? "1" : "0") + " " + packed + "\n";
        if (schema.files[(size_t)file].syntax != "proto3") {
            // Effective wire behaviour from the edition (or proto2) features.
            const int type = f.delimited ? (int)FieldType::Group : (int)f.type;
            out += pad + "  resolved " + (f.hasPresence ? "1" : "0") + " " + (f.packed ? "1" : "0")
                + " " + std::to_string(type) + " "
                + (f.type == FieldType::String ? (f.utf8Check ? "1" : "0") : "-") + " "
                + (f.required ? "1" : "0") + " "
                + (f.hasDefault ? "default=" + f.defaultValue : "-") + "\n";
        }
    }
    //=============================================================================
    std::string
    rowName(const FieldDef& field)
    {
        return field.extension.empty() ? field.name : "[" + field.extension + "]";
    }
    //=============================================================================
    std::string
    hexBytes(const std::string& bytes)
    {
        static const char digits[] = "0123456789abcdef";
        std::string out;
        out.reserve(bytes.size() * 2);
        for (unsigned char c : bytes) {
            out.push_back(digits[c >> 4]);
            out.push_back(digits[c & 15]);
        }
        return out;
    }
    //=============================================================================
    std::vector<int32_t>
    fieldsByNumber(const MessageDef& message)
    {
        std::vector<int32_t> order;
        for (size_t k = 0; k < message.fields.size(); ++k) {
            order.push_back((int32_t)k);
        }
        std::sort(order.begin(), order.end(), [&message](int32_t a, int32_t b) {
            return message.fields[(size_t)a].number < message.fields[(size_t)b].number;
        });
        return order;
    }
    //=============================================================================
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
