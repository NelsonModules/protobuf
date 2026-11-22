//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoFormat.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    enum class Special
    {
        None,
        Timestamp,
        Duration,
        Wrapper,
        Struct,
        Value,
        ListValue,
        FieldMask,
        Empty,
        Any
    };
    //=============================================================================
    Special
    specialOf(const Schema& schema, int32_t messageIndex);
    std::vector<int32_t>
    fieldsByNumber(const MessageDef& message);
    template <typename T>
    T
    slotValue(const Column& column, size_t index);
    bool
    isDefaultSlot(const Column& column, size_t index);
    std::string
    shortestDouble(double value);
    std::string
    shortestFloat(float value);
    std::string
    escapeText(const std::string& bytes, bool utf8);
    std::string
    escapeJson(const std::string& text);
    std::string
    base64Encode(const std::string& bytes);
    bool
    base64Decode(const std::string& text, std::string& bytes);
    std::string
    enumName(const Schema& schema, const FieldDef& field, int32_t number, bool& found);
    std::string
    scalarText(const Schema& schema, const FieldDef& field, const Column& column, size_t index);
    std::string
    scalarJson(const Schema& schema, const FieldDef& field, const Column& column, size_t index);
    void
    textRow(std::string& out, const Codec& codec, const ColumnSet& set, size_t row, int indent);
    // Body of a message field: "[url] { ... }" for an Any whose type is known, else its fields.
    void
    textChild(std::string& out, const Codec& codec, const ColumnSet& child, size_t row, int indent);
    void
    jsonMessage(std::string& out, const Codec& codec, const ColumnSet& set, size_t row,
        const JsonOptions& options, int level);
    void
    jsonSpecial(std::string& out, const Codec& codec, Special special, const ColumnSet& set,
        size_t row, const JsonOptions& options, int level);
    bool
    anyBodyEmpty(const std::string& out);
    // JSON of a message with no field set (map value missing in the input).
    std::string
    defaultJson(const Codec& codec, int32_t messageIndex);
    std::string
    timestampText(int64_t seconds, int32_t nanos);
    std::string
    durationText(int64_t seconds, int32_t nanos);
    bool
    parseTimestamp(const std::string& text, int64_t& seconds, int32_t& nanos);
    bool
    parseDuration(const std::string& text, int64_t& seconds, int32_t& nanos);
    // JSON range of the reference runtime: years 1 to 9999, and +/-10000 years for durations
    // with nanos of the sign of the seconds.
    bool
    validTimestamp(int64_t seconds, int32_t nanos);
    bool
    validDuration(int64_t seconds, int32_t nanos);
    // Name in the text format: the type name of a group (a delimited field named like its
    // nested message type, as proto2 groups are), else the field name.
    std::string
    textName(const Schema& schema, const MessageDef& message, const FieldDef& field);
    int64_t
    daysFromCivil(int64_t y, unsigned m, unsigned d);
    void
    civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d);
    std::string
    camelCase(const std::string& path);
    std::string
    snakeCase(const std::string& path);
    void
    fillMessage(const Codec& codec, ColumnSet& set, const JsonValue& value);
    void
    fillField(
        const Codec& codec, ColumnSet& set, size_t row, int32_t fieldIndex, const JsonValue& value);
    void
    fillSpecial(const Codec& codec, ColumnSet& set, Special special, const JsonValue& value);
    void
    putScalar(const Schema& schema, const FieldDef& field, Column& column, bool append, size_t row,
        const JsonValue& value);
    // Runs f; errors of the JSON reader and writer get "JSON: " before their message.
    template <typename F>
    auto
    withJsonPrefix(F f) -> decltype(f());
    // Message type of an Any type URL ("type.googleapis.com/pkg.Msg"), -1 when not in the schema.
    int32_t
    anyType(const Schema& schema, const std::string& typeUrl);
    //=============================================================================
    bool
    isValidUtf8Text(const std::string& text);
    class TextReader;
    class JsonReader
    {
    public:
        explicit JsonReader(const std::string& text) : text(text) { }
        JsonValue
        document();

    private:
        const std::string& text;
        size_t pos = 0;
        void
        skipSpace();
        JsonValue
        value(int depth);
        std::string
        stringLiteral();
    };
    //=============================================================================
} // namespace
//=============================================================================
std::string
formatText(const Codec& codec, const ColumnSet& set, size_t row)
{
    std::string out;
    textChild(out, codec, set, row, 0);
    return out;
}
//=============================================================================
std::string
formatJson(const Codec& codec, const ColumnSet& set, size_t row, const JsonOptions& options)
{
    std::string out;
    const Special special = specialOf(codec.schema(), set.messageIndex);
    withJsonPrefix([&]() {
        if (special != Special::None) {
            jsonSpecial(out, codec, special, set, row, options, 0);
        } else {
            jsonMessage(out, codec, set, row, options, 0);
        }
        return 0;
    });
    return out;
}
//=============================================================================
size_t
parseJson(const Codec& codec, ColumnSet& set, const std::string& text)
{
    JsonValue document = withJsonPrefix([&]() { return parseJsonValue(text); });
    const size_t before = set.rows;
    try {
        withJsonPrefix([&]() {
            if (document.type == JsonValue::Type::Array) {
                for (const JsonValue& item : document.items) {
                    fillMessage(codec, set, item);
                }
            } else {
                fillMessage(codec, set, document);
            }
            return 0;
        });
    } catch (...) {
        codec.truncate(set, before);
        throw;
    }
    codec.finish(set);
    return set.rows - before;
}
//=============================================================================
size_t
parseJsonLines(const Codec& codec, ColumnSet& set, const std::string& text)
{
    const size_t before = set.rows;
    size_t lineNumber = 0;
    size_t start = 0;
    try {
        while (start <= text.size()) {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) {
                end = text.size();
            }
            ++lineNumber;
            const std::string line = text.substr(start, end - start);
            start = end + 1;
            if (line.find_first_not_of(" \t\r") == std::string::npos) {
                continue;
            }
            try {
                JsonValue document = withJsonPrefix([&]() { return parseJsonValue(line); });
                if (document.type != JsonValue::Type::Object) {
                    Error("protobuf:jsonError:lineNotObject", "a line must hold one JSON object.");
                }
                withJsonPrefix([&]() {
                    fillMessage(codec, set, document);
                    return 0;
                });
            } catch (const ProtoError& e) {
                Error("protobuf:jsonError:atLine", "line %d: %s", lineNumber, e);
            }
        }
    } catch (...) {
        codec.truncate(set, before);
        throw;
    }
    codec.finish(set);
    return set.rows - before;
}
//=============================================================================
JsonValue
parseJsonValue(const std::string& text)
{
    JsonReader reader(text);
    return reader.document();
}
//=============================================================================
namespace {
    //=============================================================================
    Special
    specialOf(const Schema& schema, int32_t messageIndex)
    {
        const std::string& name = schema.messages[(size_t)messageIndex].fullName;
        const std::string prefix = "google.protobuf.";
        if (name.compare(0, prefix.size(), prefix) != 0) {
            return Special::None;
        }
        const std::string shortName = name.substr(prefix.size());
        if (shortName == "Timestamp") {
            return Special::Timestamp;
        }
        if (shortName == "Duration") {
            return Special::Duration;
        }
        if (shortName == "Struct") {
            return Special::Struct;
        }
        if (shortName == "Value") {
            return Special::Value;
        }
        if (shortName == "ListValue") {
            return Special::ListValue;
        }
        if (shortName == "FieldMask") {
            return Special::FieldMask;
        }
        if (shortName == "Empty") {
            return Special::Empty;
        }
        if (shortName == "Any") {
            return Special::Any;
        }
        static const char* wrappers[] = { "DoubleValue", "FloatValue", "Int64Value", "UInt64Value",
            "Int32Value", "UInt32Value", "BoolValue", "StringValue", "BytesValue" };
        for (const char* wrapper : wrappers) {
            if (shortName == wrapper) {
                return Special::Wrapper;
            }
        }
        return Special::None;
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
    template <typename T>
    T
    slotValue(const Column& column, size_t index)
    {
        T value;
        std::memcpy(&value, column.values.data() + index * sizeof(T), sizeof(T));
        return value;
    }
    //=============================================================================
    bool
    isDefaultSlot(const Column& column, size_t index)
    {
        if (column.kind == ValueKind::Bytes) {
            return column.bytes[index].empty();
        }
        const size_t width = valueSize(column.kind);
        const uint8_t* slot = column.values.data() + index * width;
        for (size_t k = 0; k < width; ++k) {
            if (slot[k] != 0) {
                return false;
            }
        }
        return true;
    }
    //=============================================================================
    // Shortest digits, laid out like Python's repr: positional for exponents -4 to 15.
    std::string
    layoutDigits(const char* begin, const char* end)
    {
        std::string text(begin, end);
        std::string sign;
        if (!text.empty() && text[0] == '-') {
            sign = "-";
            text.erase(0, 1);
        }
        size_t e = text.find('e');
        int exponent = std::atoi(text.c_str() + e + 1);
        std::string digits;
        for (size_t k = 0; k < e; ++k) {
            if (text[k] != '.') {
                digits.push_back(text[k]);
            }
        }
        if (exponent >= -4 && exponent < 16) {
            const int point = exponent + 1;
            std::string out;
            if (point <= 0) {
                out = "0." + std::string((size_t)(-point), '0') + digits;
            } else if ((size_t)point >= digits.size()) {
                out = digits + std::string((size_t)point - digits.size(), '0') + ".0";
            } else {
                out = digits.substr(0, (size_t)point) + "." + digits.substr((size_t)point);
            }
            return sign + out;
        }
        std::string out = digits.substr(0, 1);
        if (digits.size() > 1) {
            out += "." + digits.substr(1);
        }
        char power[16];
        std::snprintf(
            power, sizeof(power), "e%c%02d", exponent < 0 ? '-' : '+', std::abs(exponent));
        return sign + out + power;
    }
    //=============================================================================
    std::string
    shortestDouble(double value)
    {
        if (std::isnan(value)) {
            return "nan";
        }
        if (std::isinf(value)) {
            return value < 0 ? "-inf" : "inf";
        }
        char buffer[64];
        auto result
            = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
        return layoutDigits(buffer, result.ptr);
    }
    //=============================================================================
    std::string
    shortestFloat(float value)
    {
        if (std::isnan(value)) {
            return "nan";
        }
        if (std::isinf(value)) {
            return value < 0 ? "-inf" : "inf";
        }
        char buffer[64];
        auto result
            = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
        return layoutDigits(buffer, result.ptr);
    }
    //=============================================================================
    // C-style escapes of the text format; utf8 keeps bytes 0x80-0xFF of string fields.
    std::string
    escapeText(const std::string& bytes, bool utf8)
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
                if (c < 0x20 || c == 0x7F || (c >= 0x80 && !utf8)) {
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
    std::string
    escapeJson(const std::string& text)
    {
        std::string out = "\"";
        for (unsigned char c : text) {
            switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            default:
                if (c < 0x20) {
                    char code[8];
                    std::snprintf(code, sizeof(code), "\\u%04x", c);
                    out += code;
                } else {
                    out.push_back((char)c);
                }
            }
        }
        return out + "\"";
    }
    //=============================================================================
    std::string
    base64Encode(const std::string& bytes)
    {
        static const char alphabet[]
            = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        size_t k = 0;
        for (; k + 2 < bytes.size(); k += 3) {
            const uint32_t n = ((uint32_t)(unsigned char)bytes[k] << 16)
                | ((uint32_t)(unsigned char)bytes[k + 1] << 8) | (unsigned char)bytes[k + 2];
            out.push_back(alphabet[(n >> 18) & 63]);
            out.push_back(alphabet[(n >> 12) & 63]);
            out.push_back(alphabet[(n >> 6) & 63]);
            out.push_back(alphabet[n & 63]);
        }
        if (k < bytes.size()) {
            uint32_t n = (uint32_t)(unsigned char)bytes[k] << 16;
            if (k + 1 < bytes.size()) {
                n |= (uint32_t)(unsigned char)bytes[k + 1] << 8;
            }
            out.push_back(alphabet[(n >> 18) & 63]);
            out.push_back(alphabet[(n >> 12) & 63]);
            out.push_back(k + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=');
            out.push_back('=');
        }
        return out;
    }
    //=============================================================================
    // Standard and URL-safe alphabets, padding optional.
    bool
    base64Decode(const std::string& text, std::string& bytes)
    {
        uint32_t buffer = 0;
        int bits = 0;
        bytes.clear();
        for (char c : text) {
            int v;
            if (c >= 'A' && c <= 'Z') {
                v = c - 'A';
            } else if (c >= 'a' && c <= 'z') {
                v = c - 'a' + 26;
            } else if (c >= '0' && c <= '9') {
                v = c - '0' + 52;
            } else if (c == '+' || c == '-') {
                v = 62;
            } else if (c == '/' || c == '_') {
                v = 63;
            } else if (c == '=') {
                break;
            } else {
                return false;
            }
            buffer = (buffer << 6) | (uint32_t)v;
            bits += 6;
            if (bits >= 8) {
                bits -= 8;
                bytes.push_back((char)((buffer >> bits) & 0xFF));
            }
        }
        return true;
    }
    //=============================================================================
    std::string
    enumName(const Schema& schema, const FieldDef& field, int32_t number, bool& found)
    {
        for (const EnumValueDef& v : schema.enums[(size_t)field.enumIndex].values) {
            if (v.number == number) {
                found = true;
                return v.name;
            }
        }
        found = false;
        return std::to_string(number);
    }
    //=============================================================================
    std::string
    integerText(const FieldDef& field, const Column& column, size_t index)
    {
        switch (column.kind) {
        case ValueKind::Int64:
            return std::to_string(slotValue<int64_t>(column, index));
        case ValueKind::UInt64:
            return std::to_string(slotValue<uint64_t>(column, index));
        case ValueKind::Int32:
            return std::to_string(slotValue<int32_t>(column, index));
        case ValueKind::UInt32:
            return std::to_string(slotValue<uint32_t>(column, index));
        default:
            (void)field;
            return "0";
        }
    }
    //=============================================================================
    std::string
    scalarText(const Schema& schema, const FieldDef& field, const Column& column, size_t index)
    {
        switch (column.kind) {
        case ValueKind::Double:
            return shortestDouble(slotValue<double>(column, index));
        case ValueKind::Float:
            return shortestFloat(slotValue<float>(column, index));
        case ValueKind::Bool:
            return column.values[index] ? "true" : "false";
        case ValueKind::Bytes:
            return "\"" + escapeText(column.bytes[index], field.type == FieldType::String) + "\"";
        default:
            break;
        }
        if (field.type == FieldType::Enum) {
            bool found = false;
            return enumName(schema, field, slotValue<int32_t>(column, index), found);
        }
        return integerText(field, column, index);
    }
    //=============================================================================
    std::string
    scalarJson(const Schema& schema, const FieldDef& field, const Column& column, size_t index)
    {
        switch (column.kind) {
        case ValueKind::Double: {
            const double v = slotValue<double>(column, index);
            if (std::isnan(v)) {
                return "\"NaN\"";
            }
            if (std::isinf(v)) {
                return v < 0 ? "\"-Infinity\"" : "\"Infinity\"";
            }
            return shortestDouble(v);
        }
        case ValueKind::Float: {
            const float v = slotValue<float>(column, index);
            if (std::isnan(v)) {
                return "\"NaN\"";
            }
            if (std::isinf(v)) {
                return v < 0 ? "\"-Infinity\"" : "\"Infinity\"";
            }
            return shortestFloat(v);
        }
        case ValueKind::Bool:
            return column.values[index] ? "true" : "false";
        case ValueKind::Bytes:
            return field.type == FieldType::String
                ? escapeJson(column.bytes[index])
                : "\"" + base64Encode(column.bytes[index]) + "\"";
        case ValueKind::Int64:
        case ValueKind::UInt64:
            return "\"" + integerText(field, column, index) + "\"";
        default:
            break;
        }
        if (field.type == FieldType::Enum) {
            const int32_t number = slotValue<int32_t>(column, index);
            if (schema.enums[(size_t)field.enumIndex].fullName == "google.protobuf.NullValue") {
                return "null";
            }
            bool found = false;
            std::string name = enumName(schema, field, number, found);
            return found ? escapeJson(name) : name;
        }
        return integerText(field, column, index);
    }
    //=============================================================================
    // Map entries of a row, ordered by key (text format prints maps sorted).
    std::vector<size_t>
    sortedEntries(const ColumnSet& entries, size_t first, size_t last)
    {
        std::vector<size_t> order;
        for (size_t k = first; k < last; ++k) {
            order.push_back(k);
        }
        const Column& keys = entries.columns[0];
        std::stable_sort(order.begin(), order.end(), [&keys](size_t a, size_t b) {
            switch (keys.kind) {
            case ValueKind::Bytes:
                return keys.bytes[a] < keys.bytes[b];
            case ValueKind::Int64:
                return slotValue<int64_t>(keys, a) < slotValue<int64_t>(keys, b);
            case ValueKind::UInt64:
                return slotValue<uint64_t>(keys, a) < slotValue<uint64_t>(keys, b);
            case ValueKind::Int32:
                return slotValue<int32_t>(keys, a) < slotValue<int32_t>(keys, b);
            case ValueKind::UInt32:
                return slotValue<uint32_t>(keys, a) < slotValue<uint32_t>(keys, b);
            default:
                return keys.values[a] < keys.values[b];
            }
        });
        return order;
    }
    //=============================================================================
    void
    textRow(std::string& out, const Codec& codec, const ColumnSet& set, size_t row, int indent)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        const std::string pad((size_t)indent, ' ');
        for (int32_t fieldIndex : fieldsByNumber(message)) {
            const FieldDef& field = message.fields[(size_t)fieldIndex];
            const Column& column = set.columns[(size_t)fieldIndex];
            if (column.repeated) {
                const size_t first = (size_t)column.offsets[row];
                const size_t last = (size_t)column.offsets[row + 1];
                if (first == last) {
                    continue;
                }
                if (column.kind == ValueKind::Message) {
                    std::vector<size_t> order;
                    if (field.isMap) {
                        order = sortedEntries(*column.child, first, last);
                    } else {
                        for (size_t k = first; k < last; ++k) {
                            order.push_back(k);
                        }
                    }
                    for (size_t k : order) {
                        out += pad + textName(schema, message, field) + " {\n";
                        textChild(out, codec, *column.child, k, indent + 2);
                        out += pad + "}\n";
                    }
                } else {
                    for (size_t k = first; k < last; ++k) {
                        out += pad + textName(schema, message, field) + ": "
                            + scalarText(schema, field, column, k) + "\n";
                    }
                }
                continue;
            }
            if (column.kind == ValueKind::Message) {
                if (column.childRow[row] >= 0) {
                    out += pad + textName(schema, message, field) + " {\n";
                    textChild(out, codec, *column.child, (size_t)column.childRow[row], indent + 2);
                    out += pad + "}\n";
                }
                continue;
            }
            const bool present
                = column.hasPresence ? column.presence[row] != 0 : !isDefaultSlot(column, row);
            if (present) {
                out += pad + textName(schema, message, field) + ": "
                    + scalarText(schema, field, column, row) + "\n";
            }
        }
    }
    //=============================================================================
    void
    textChild(std::string& out, const Codec& codec, const ColumnSet& child, size_t row, int indent)
    {
        const Schema& schema = codec.schema();
        if (specialOf(schema, child.messageIndex) == Special::Any) {
            const std::string& url = child.columns[0].bytes[row];
            const std::string& packed = child.columns[1].bytes[row];
            const int32_t type = anyType(schema, url);
            if (type >= 0) {
                ColumnSet inner = codec.createColumnSet(type);
                try {
                    codec.appendMessage(
                        inner, reinterpret_cast<const uint8_t*>(packed.data()), packed.size());
                    const std::string pad((size_t)indent, ' ');
                    out += pad + "[" + url + "] {\n";
                    textRow(out, codec, inner, 0, indent + 2);
                    out += pad + "}\n";
                    return;
                } catch (const ProtoError&) {
                    // Bytes that do not decode: printed as the raw fields of the Any.
                }
            }
        }
        textRow(out, codec, child, row, indent);
    }
    //=============================================================================
    void
    newline(std::string& out, const JsonOptions& options, int level)
    {
        if (options.indent) {
            out += "\n" + std::string((size_t)(2 * level), ' ');
        }
    }
    //=============================================================================
    // Value of a message field element: special JSON form or object.
    void
    jsonChild(std::string& out, const Codec& codec, const ColumnSet& child, size_t row,
        const JsonOptions& options, int level)
    {
        const Special special = specialOf(codec.schema(), child.messageIndex);
        if (special != Special::None) {
            jsonSpecial(out, codec, special, child, row, options, level);
        } else {
            jsonMessage(out, codec, child, row, options, level);
        }
    }
    //=============================================================================
    void
    jsonMessage(std::string& out, const Codec& codec, const ColumnSet& set, size_t row,
        const JsonOptions& options, int level)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        const std::string colon = options.indent ? ": " : ":";
        out += "{";
        bool firstMember = true;
        auto member = [&](const FieldDef& field) {
            out += firstMember ? "" : ",";
            firstMember = false;
            newline(out, options, level + 1);
            out += escapeJson(!field.extension.empty()
                           ? "[" + field.extension + "]"
                           : (options.protoNames ? field.name : field.jsonName))
                + colon;
        };
        for (int32_t fieldIndex : fieldsByNumber(message)) {
            const FieldDef& field = message.fields[(size_t)fieldIndex];
            const Column& column = set.columns[(size_t)fieldIndex];
            if (column.repeated) {
                const size_t first = (size_t)column.offsets[row];
                const size_t last = (size_t)column.offsets[row + 1];
                if (first == last) {
                    continue;
                }
                member(field);
                if (field.isMap) {
                    const ColumnSet& entries = *column.child;
                    const MessageDef& entryDef = schema.messages[(size_t)entries.messageIndex];
                    out += "{";
                    for (size_t k = first; k < last; ++k) {
                        out += k == first ? "" : ",";
                        newline(out, options, level + 2);
                        const Column& keys = entries.columns[0];
                        std::string key = keys.kind == ValueKind::Bytes
                            ? keys.bytes[k]
                            : (keys.kind == ValueKind::Bool
                                      ? (keys.values[k] ? "true" : "false")
                                      : integerText(entryDef.fields[0], keys, k));
                        out += escapeJson(key) + colon;
                        const Column& values = entries.columns[1];
                        if (values.kind == ValueKind::Message) {
                            if (values.childRow[k] < 0) {
                                out += defaultJson(codec, entryDef.fields[1].messageIndex);
                            } else {
                                jsonChild(out, codec, *values.child, (size_t)values.childRow[k],
                                    options, level + 2);
                            }
                        } else {
                            out += scalarJson(schema, entryDef.fields[1], values, k);
                        }
                    }
                    newline(out, options, level + 1);
                    out += "}";
                    continue;
                }
                out += "[";
                for (size_t k = first; k < last; ++k) {
                    out += k == first ? "" : ",";
                    newline(out, options, level + 2);
                    if (column.kind == ValueKind::Message) {
                        jsonChild(out, codec, *column.child, k, options, level + 2);
                    } else {
                        out += scalarJson(schema, field, column, k);
                    }
                }
                newline(out, options, level + 1);
                out += "]";
                continue;
            }
            if (column.kind == ValueKind::Message) {
                if (column.childRow[row] >= 0) {
                    member(field);
                    jsonChild(out, codec, *column.child, (size_t)column.childRow[row], options,
                        level + 1);
                }
                continue;
            }
            const bool present
                = column.hasPresence ? column.presence[row] != 0 : !isDefaultSlot(column, row);
            if (present) {
                member(field);
                out += scalarJson(schema, field, column, row);
            }
        }
        if (!firstMember) {
            newline(out, options, level);
        }
        out += "}";
    }
    //=============================================================================
    std::string
    defaultJson(const Codec& codec, int32_t messageIndex)
    {
        ColumnSet empty = codec.createColumnSet(messageIndex);
        codec.beginRow(empty);
        codec.endRow(empty);
        return formatJson(codec, empty, 0, JsonOptions());
    }
    //=============================================================================
    void
    jsonSpecial(std::string& out, const Codec& codec, Special special, const ColumnSet& set,
        size_t row, const JsonOptions& options, int level)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        switch (special) {
        case Special::Timestamp:
        case Special::Duration: {
            const int64_t seconds = slotValue<int64_t>(set.columns[0], row);
            const int32_t nanos = slotValue<int32_t>(set.columns[1], row);
            if (special == Special::Timestamp && !validTimestamp(seconds, nanos)) {
                Error("protobuf:jsonError:timestampOutOfRange",
                    "Timestamp out of the JSON range (years 1 to 9999).");
            }
            if (special == Special::Duration && !validDuration(seconds, nanos)) {
                Error("protobuf:jsonError:durationOutOfRange",
                    "Duration out of the JSON range (+/-10000 years).");
            }
            out += "\""
                + (special == Special::Timestamp ? timestampText(seconds, nanos)
                                                 : durationText(seconds, nanos))
                + "\"";
            return;
        }
        case Special::Wrapper:
            out += scalarJson(schema, message.fields[0], set.columns[0], row);
            return;
        case Special::Empty:
            out += "{}";
            return;
        case Special::Any: {
            // {"@type": url, fields of the packed message} or "value" for a special type.
            const std::string& url = set.columns[0].bytes[row];
            const std::string& packed = set.columns[1].bytes[row];
            const int32_t type = anyType(schema, url);
            if (type < 0) {
                jsonMessage(out, codec, set, row, options, level);
                return;
            }
            ColumnSet inner = codec.createColumnSet(type);
            codec.appendMessage(
                inner, reinterpret_cast<const uint8_t*>(packed.data()), packed.size());
            const std::string colon = options.indent ? ": " : ":";
            out += "{";
            newline(out, options, level + 1);
            out += escapeJson("@type") + colon + escapeJson(url);
            const Special innerSpecial = specialOf(schema, type);
            if (innerSpecial != Special::None) {
                out += ",";
                newline(out, options, level + 1);
                out += escapeJson("value") + colon;
                jsonSpecial(out, codec, innerSpecial, inner, 0, options, level + 1);
            } else {
                std::string body;
                jsonMessage(body, codec, inner, 0, options, level);
                // Members of the packed message after "@type" (body is "{...}").
                if (body.size() > 2) {
                    out += "," + body.substr(1, body.size() - 2);
                }
            }
            if (!options.indent || innerSpecial != Special::None || anyBodyEmpty(out)) {
                newline(out, options, level);
            }
            out += "}";
            return;
        }
        case Special::FieldMask: {
            const Column& paths = set.columns[0];
            std::string joined;
            for (size_t k = (size_t)paths.offsets[row]; k < (size_t)paths.offsets[row + 1]; ++k) {
                joined += (joined.empty() ? "" : ",") + camelCase(paths.bytes[k]);
            }
            out += escapeJson(joined);
            return;
        }
        case Special::Struct: {
            // map<string, Value> fields = 1: an object.
            const Column& fields = set.columns[0];
            const ColumnSet& entries = *fields.child;
            const std::string colon = options.indent ? ": " : ":";
            out += "{";
            for (size_t k = (size_t)fields.offsets[row]; k < (size_t)fields.offsets[row + 1]; ++k) {
                out += k == (size_t)fields.offsets[row] ? "" : ",";
                newline(out, options, level + 1);
                out += escapeJson(entries.columns[0].bytes[k]) + colon;
                const Column& values = entries.columns[1];
                if (values.childRow[k] >= 0) {
                    jsonSpecial(out, codec, Special::Value, *values.child,
                        (size_t)values.childRow[k], options, level + 1);
                } else {
                    out += "null";
                }
            }
            if (fields.offsets[row + 1] > fields.offsets[row]) {
                newline(out, options, level);
            }
            out += "}";
            return;
        }
        case Special::ListValue: {
            const Column& values = set.columns[0];
            out += "[";
            for (size_t k = (size_t)values.offsets[row]; k < (size_t)values.offsets[row + 1]; ++k) {
                out += k == (size_t)values.offsets[row] ? "" : ",";
                newline(out, options, level + 1);
                jsonSpecial(out, codec, Special::Value, *values.child, k, options, level + 1);
            }
            if (values.offsets[row + 1] > values.offsets[row]) {
                newline(out, options, level);
            }
            out += "]";
            return;
        }
        case Special::Value: {
            // oneof kind: null_value, number_value, string_value, bool_value, struct_value,
            // list_value.
            const int32_t active = set.oneofCase.empty() ? -1 : set.oneofCase[0][row];
            if (active < 0) {
                out += "null";
                return;
            }
            const FieldDef& field = message.fields[(size_t)active];
            const Column& column = set.columns[(size_t)active];
            if (field.name == "null_value") {
                out += "null";
            } else if (column.kind == ValueKind::Message) {
                jsonChild(out, codec, *column.child, (size_t)column.childRow[row], options, level);
            } else if (column.kind == ValueKind::Double) {
                out += shortestDouble(slotValue<double>(column, row));
            } else {
                out += scalarJson(schema, field, column, row);
            }
            return;
        }
        default:
            jsonMessage(out, codec, set, row, options, level);
        }
    }
    //=============================================================================
    // True when the text ends right after the "@type" member (nothing else was written).
    bool
    anyBodyEmpty(const std::string& out)
    {
        return !out.empty() && out.back() == '"';
    }
    //=============================================================================
    int32_t
    anyType(const Schema& schema, const std::string& typeUrl)
    {
        const size_t slash = typeUrl.find_last_of('/');
        return schema.findMessage(slash == std::string::npos ? typeUrl : typeUrl.substr(slash + 1));
    }
    //=============================================================================
    std::string
    fraction(int32_t nanos)
    {
        if (nanos == 0) {
            return "";
        }
        char buffer[16];
        if (nanos % 1000000 == 0) {
            std::snprintf(buffer, sizeof(buffer), ".%03d", nanos / 1000000);
        } else if (nanos % 1000 == 0) {
            std::snprintf(buffer, sizeof(buffer), ".%06d", nanos / 1000);
        } else {
            std::snprintf(buffer, sizeof(buffer), ".%09d", nanos);
        }
        return buffer;
    }
    //=============================================================================
    std::string
    timestampText(int64_t seconds, int32_t nanos)
    {
        int64_t days = seconds / 86400;
        int64_t rest = seconds % 86400;
        if (rest < 0) {
            rest += 86400;
            days--;
        }
        int64_t y = 0;
        unsigned m = 0;
        unsigned d = 0;
        civilFromDays(days, y, m, d);
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%04lld-%02u-%02uT%02d:%02d:%02d", (long long)y, m, d,
            (int)(rest / 3600), (int)(rest / 60 % 60), (int)(rest % 60));
        return std::string(buffer) + fraction(nanos) + "Z";
    }
    //=============================================================================
    std::string
    durationText(int64_t seconds, int32_t nanos)
    {
        const bool negative = seconds < 0 || nanos < 0;
        const uint64_t whole = (uint64_t)(seconds < 0 ? -seconds : seconds);
        return (negative ? "-" : "") + std::to_string(whole) + fraction(nanos < 0 ? -nanos : nanos)
            + "s";
    }
    //=============================================================================
    bool
    digitsAt(const std::string& text, size_t pos, size_t count, int64_t& value)
    {
        if (pos + count > text.size()) {
            return false;
        }
        value = 0;
        for (size_t k = 0; k < count; ++k) {
            const char c = text[pos + k];
            if (c < '0' || c > '9') {
                return false;
            }
            value = value * 10 + (c - '0');
        }
        return true;
    }
    //=============================================================================
    // Fractional seconds after a '.', 1 to 9 digits; pos moves past them.
    bool
    readFraction(const std::string& text, size_t& pos, int32_t& nanos)
    {
        nanos = 0;
        if (pos >= text.size() || text[pos] != '.') {
            return true;
        }
        pos++;
        int digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (digits == 9) {
                return false;
            }
            nanos = nanos * 10 + (text[pos] - '0');
            digits++;
            pos++;
        }
        if (digits == 0) {
            return false;
        }
        for (; digits < 9; ++digits) {
            nanos *= 10;
        }
        return true;
    }
    //=============================================================================
    // RFC 3339: 2026-11-12T10:30:15.123Z or with a +hh:mm / -hh:mm offset.
    bool
    parseTimestamp(const std::string& text, int64_t& seconds, int32_t& nanos)
    {
        int64_t y, mo, d, h, mi, s;
        if (!digitsAt(text, 0, 4, y) || text.size() < 20 || text[4] != '-'
            || !digitsAt(text, 5, 2, mo) || text[7] != '-' || !digitsAt(text, 8, 2, d)
            || (text[10] != 'T' && text[10] != 't') || !digitsAt(text, 11, 2, h) || text[13] != ':'
            || !digitsAt(text, 14, 2, mi) || text[16] != ':' || !digitsAt(text, 17, 2, s)) {
            return false;
        }
        size_t pos = 19;
        if (!readFraction(text, pos, nanos) || pos >= text.size()) {
            return false;
        }
        int64_t offset = 0;
        if (text[pos] == 'Z' || text[pos] == 'z') {
            pos++;
        } else if (text[pos] == '+' || text[pos] == '-') {
            int64_t oh, om;
            if (!digitsAt(text, pos + 1, 2, oh) || pos + 3 >= text.size() || text[pos + 3] != ':'
                || !digitsAt(text, pos + 4, 2, om)) {
                return false;
            }
            offset = (oh * 3600 + om * 60) * (text[pos] == '+' ? 1 : -1);
            pos += 6;
        } else {
            return false;
        }
        if (pos != text.size() || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59
            || s > 59) {
            return false;
        }
        seconds
            = daysFromCivil(y, (unsigned)mo, (unsigned)d) * 86400 + h * 3600 + mi * 60 + s - offset;
        return validTimestamp(seconds, nanos);
    }
    //=============================================================================
    std::string
    textName(const Schema& schema, const MessageDef& message, const FieldDef& field)
    {
        if (!field.extension.empty()) {
            return "[" + field.extension + "]";
        }
        if (!field.delimited || field.messageIndex < 0) {
            return field.name;
        }
        const MessageDef& type = schema.messages[(size_t)field.messageIndex];
        std::string lower = type.name;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        if (lower == field.name && type.fullName == message.fullName + "." + type.name) {
            return type.name;
        }
        return field.name;
    }
    //=============================================================================
    bool
    validTimestamp(int64_t seconds, int32_t nanos)
    {
        return seconds >= -62135596800LL && seconds <= 253402300799LL && nanos >= 0
            && nanos <= 999999999;
    }
    //=============================================================================
    bool
    validDuration(int64_t seconds, int32_t nanos)
    {
        return seconds >= -315576000000LL && seconds <= 315576000000LL && nanos >= -999999999
            && nanos <= 999999999 && !(seconds > 0 && nanos < 0) && !(seconds < 0 && nanos > 0);
    }
    //=============================================================================
    bool
    parseDuration(const std::string& text, int64_t& seconds, int32_t& nanos)
    {
        if (text.size() < 2 || text.back() != 's') {
            return false;
        }
        size_t pos = 0;
        const bool negative = text[0] == '-';
        if (negative) {
            pos++;
        }
        const size_t start = pos;
        uint64_t whole = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            whole = whole * 10 + (uint64_t)(text[pos] - '0');
            pos++;
        }
        if (pos == start || pos - start > 12 || !readFraction(text, pos, nanos)
            || pos != text.size() - 1) {
            return false;
        }
        seconds = negative ? -(int64_t)whole : (int64_t)whole;
        if (negative) {
            nanos = -nanos;
        }
        return validDuration(seconds, nanos);
    }
    //=============================================================================
    // Days since 1970-01-01 of a proleptic Gregorian date (H. Hinnant's algorithm).
    int64_t
    daysFromCivil(int64_t y, unsigned m, unsigned d)
    {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int64_t)doe - 719468;
    }
    //=============================================================================
    void
    civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d)
    {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = (unsigned)(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        y = (int64_t)yoe + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1;
        m = mp < 10 ? mp + 3 : mp - 9;
        y += m <= 2;
    }
    //=============================================================================
    std::string
    camelCase(const std::string& path)
    {
        std::string out;
        bool upper = false;
        for (char c : path) {
            if (c == '_') {
                upper = true;
            } else {
                out.push_back(upper && c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c);
                upper = false;
            }
        }
        return out;
    }
    //=============================================================================
    std::string
    snakeCase(const std::string& path)
    {
        std::string out;
        for (char c : path) {
            if (c >= 'A' && c <= 'Z') {
                out.push_back('_');
                out.push_back((char)(c - 'A' + 'a'));
            } else {
                out.push_back(c);
            }
        }
        return out;
    }
    //=============================================================================
    template <typename F>
    auto
    withJsonPrefix(F f) -> decltype(f())
    {
        try {
            return f();
        } catch (const ProtoError& e) {
            if (e.id().rfind("protobuf:jsonError:", 0) == 0) {
                Error("protobuf:jsonError:prefix", "JSON: %s", e);
            }
            throw;
        }
    }
    //=============================================================================
    int32_t
    fieldByJsonName(const MessageDef& message, const std::string& name)
    {
        for (size_t k = 0; k < message.fields.size(); ++k) {
            const FieldDef& f = message.fields[k];
            if (f.extension.empty() ? (f.jsonName == name || f.name == name)
                                    : name == "[" + f.extension + "]") {
                return (int32_t)k;
            }
        }
        return -1;
    }
    //=============================================================================
    void
    fillMessage(const Codec& codec, ColumnSet& set, const JsonValue& value)
    {
        const Schema& schema = codec.schema();
        const Special special = specialOf(schema, set.messageIndex);
        if (special != Special::None && special != Special::Empty) {
            fillSpecial(codec, set, special, value);
            return;
        }
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        if (value.type != JsonValue::Type::Object) {
            Error("protobuf:jsonError:messageExpectsObject", "message \"%s\" expects an object.",
                message.fullName);
        }
        codec.beginRow(set);
        const size_t row = set.rows - 1;
        for (const auto& member : value.members) {
            const int32_t fieldIndex = fieldByJsonName(message, member.first);
            if (fieldIndex < 0) {
                Error("protobuf:jsonError:unknownFieldMessage",
                    "unknown field \"%s\" for message \"%s\".", member.first, message.fullName);
            }
            fillField(codec, set, row, fieldIndex, member.second);
        }
        codec.endRow(set);
    }
    //=============================================================================
    void
    setOneofCase(ColumnSet& set, const MessageDef& message, size_t row, int32_t fieldIndex)
    {
        const int32_t oneof = message.fields[(size_t)fieldIndex].oneofIndex;
        for (size_t k = 0; k < set.oneofIndices.size(); ++k) {
            if (set.oneofIndices[k] == oneof) {
                if (set.oneofCase[k][row] >= 0 && set.oneofCase[k][row] != fieldIndex) {
                    Error("protobuf:jsonError:severalMembersOneofSet",
                        "several members of oneof \"%s\" are set.",
                        message.oneofs[(size_t)oneof].name);
                }
                set.oneofCase[k][row] = fieldIndex;
            }
        }
    }
    //=============================================================================
    void
    fillField(
        const Codec& codec, ColumnSet& set, size_t row, int32_t fieldIndex, const JsonValue& value)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        const FieldDef& field = message.fields[(size_t)fieldIndex];
        Column& column = set.columns[(size_t)fieldIndex];
        const bool nullValue = field.kind == ValueKind::Message
            && specialOf(schema, field.messageIndex) == Special::Value;
        if (value.type == JsonValue::Type::Null && !nullValue) {
            return;
        }
        if (field.isMap) {
            if (value.type != JsonValue::Type::Object) {
                Error("protobuf:jsonError:fieldExpectsObject", "field \"%s\" expects an object.",
                    field.name);
            }
            ColumnSet& entries = codec.childSet(column, field.messageIndex);
            const MessageDef& entryDef = schema.messages[(size_t)field.messageIndex];
            for (const auto& member : value.members) {
                codec.beginRow(entries);
                const size_t entry = entries.rows - 1;
                JsonValue key;
                key.type = entryDef.fields[0].kind == ValueKind::Bool ? JsonValue::Type::Bool
                                                                      : JsonValue::Type::String;
                key.text = member.first;
                if (key.type == JsonValue::Type::Bool) {
                    if (member.first != "true" && member.first != "false") {
                        Error("protobuf:jsonError:mapKeyBool", "map key \"%s\" is not a bool.",
                            member.first);
                    }
                    key.boolean = member.first == "true";
                }
                putScalar(schema, entryDef.fields[0], entries.columns[0], false, entry, key);
                // Map keys have explicit presence in editions files.
                if (entries.columns[0].hasPresence) {
                    entries.columns[0].presence[entry] = 1;
                }
                fillField(codec, entries, entry, 1, member.second);
                codec.endRow(entries);
            }
            return;
        }
        if (column.repeated) {
            if (value.type != JsonValue::Type::Array) {
                Error("protobuf:jsonError:fieldExpectsArray", "field \"%s\" expects an array.",
                    field.name);
            }
            for (const JsonValue& item : value.items) {
                if (field.kind == ValueKind::Message) {
                    fillMessage(codec, codec.childSet(column, field.messageIndex), item);
                } else {
                    putScalar(schema, field, column, true, 0, item);
                }
            }
            return;
        }
        if (field.oneofIndex >= 0 && !message.oneofs[(size_t)field.oneofIndex].synthetic) {
            setOneofCase(set, message, row, fieldIndex);
        }
        if (field.kind == ValueKind::Message) {
            ColumnSet& child = codec.childSet(column, field.messageIndex);
            fillMessage(codec, child, value);
            column.childRow[row] = (int64_t)child.rows - 1;
            return;
        }
        putScalar(schema, field, column, false, row, value);
        if (column.hasPresence) {
            column.presence[row] = 1;
        }
    }
    //=============================================================================
    void
    fillSpecial(const Codec& codec, ColumnSet& set, Special special, const JsonValue& value)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        codec.beginRow(set);
        const size_t row = set.rows - 1;
        switch (special) {
        case Special::Any: {
            if (value.type != JsonValue::Type::Object) {
                Error("protobuf:jsonError:anyExpectsObject", "Any expects an object.");
            }
            auto typeMember = std::find_if(value.members.begin(), value.members.end(),
                [](const auto& m) { return m.first == "@type"; });
            if (typeMember == value.members.end()
                || typeMember->second.type != JsonValue::Type::String) {
                Error("protobuf:jsonError:anyNeedsTypeString", "Any needs a \"@type\" string.");
            }
            const std::string url = typeMember->second.text;
            const int32_t type = anyType(schema, url);
            if (type < 0) {
                Error("protobuf:jsonError:typeSchema", "type of \"%s\" is not in the schema.", url);
            }
            ColumnSet inner = codec.createColumnSet(type);
            const Special innerSpecial = specialOf(schema, type);
            JsonValue body;
            if (innerSpecial != Special::None) {
                auto valueMember = std::find_if(value.members.begin(), value.members.end(),
                    [](const auto& m) { return m.first == "value"; });
                if (valueMember == value.members.end()) {
                    Error("protobuf:jsonError:anyNeedsValueMember",
                        "Any of \"%s\" needs a \"value\" member.", url);
                }
                body = valueMember->second;
            } else {
                body.type = JsonValue::Type::Object;
                for (const auto& member : value.members) {
                    if (member.first != "@type") {
                        body.members.push_back(member);
                    }
                }
            }
            fillMessage(codec, inner, body);
            codec.finish(inner);
            set.columns[0].bytes[row] = url;
            set.columns[1].bytes[row] = codec.encodeRow(inner, 0);
        } break;
        case Special::Timestamp:
        case Special::Duration: {
            int64_t seconds = 0;
            int32_t nanos = 0;
            const bool ok = value.type == JsonValue::Type::String
                && (special == Special::Timestamp ? parseTimestamp(value.text, seconds, nanos)
                                                  : parseDuration(value.text, seconds, nanos));
            if (!ok) {
                Error("protobuf:jsonError:invalidValue", "invalid %s value.", message.name);
            }
            std::memcpy(set.columns[0].values.data() + row * 8, &seconds, 8);
            std::memcpy(set.columns[1].values.data() + row * 4, &nanos, 4);
        } break;
        case Special::Wrapper:
            putScalar(schema, message.fields[0], set.columns[0], false, row, value);
            break;
        case Special::FieldMask: {
            if (value.type != JsonValue::Type::String) {
                Error("protobuf:jsonError:fieldmaskExpectsString", "FieldMask expects a string.");
            }
            Column& paths = set.columns[0];
            for (size_t start = 0; !value.text.empty() && start <= value.text.size();) {
                size_t stop = value.text.find(',', start);
                stop = stop == std::string::npos ? value.text.size() : stop;
                paths.bytes.push_back(snakeCase(value.text.substr(start, stop - start)));
                start = stop + 1;
            }
        } break;
        case Special::Struct: {
            if (value.type != JsonValue::Type::Object) {
                Error("protobuf:jsonError:structExpectsObject", "Struct expects an object.");
            }
            fillField(codec, set, row, 0, value);
        } break;
        case Special::ListValue: {
            if (value.type != JsonValue::Type::Array) {
                Error("protobuf:jsonError:listvalueExpectsArray", "ListValue expects an array.");
            }
            fillField(codec, set, row, 0, value);
        } break;
        case Special::Value: {
            auto set_member = [&](const char* name) {
                const int32_t index = message.findFieldByName(name);
                setOneofCase(set, message, row, index);
                return index;
            };
            switch (value.type) {
            case JsonValue::Type::Null: {
                const int32_t index = set_member("null_value");
                set.columns[(size_t)index].presence[row] = 1;
            } break;
            case JsonValue::Type::Number: {
                const int32_t index = set_member("number_value");
                std::memcpy(set.columns[(size_t)index].values.data() + row * 8, &value.number, 8);
                set.columns[(size_t)index].presence[row] = 1;
            } break;
            case JsonValue::Type::String: {
                const int32_t index = set_member("string_value");
                set.columns[(size_t)index].bytes[row] = value.text;
                set.columns[(size_t)index].presence[row] = 1;
            } break;
            case JsonValue::Type::Bool: {
                const int32_t index = set_member("bool_value");
                set.columns[(size_t)index].values[row] = value.boolean ? 1 : 0;
                set.columns[(size_t)index].presence[row] = 1;
            } break;
            case JsonValue::Type::Object:
            case JsonValue::Type::Array: {
                const int32_t index = set_member(
                    value.type == JsonValue::Type::Object ? "struct_value" : "list_value");
                const FieldDef& field = message.fields[(size_t)index];
                Column& column = set.columns[(size_t)index];
                ColumnSet& child = codec.childSet(column, field.messageIndex);
                fillSpecial(codec, child,
                    value.type == JsonValue::Type::Object ? Special::Struct : Special::ListValue,
                    value);
                column.childRow[row] = (int64_t)child.rows - 1;
            } break;
            }
        } break;
        default:
            break;
        }
        codec.endRow(set);
    }
    //=============================================================================
    // Integer of a JSON number or string, within [low, high].
    template <typename T>
    T
    jsonInteger(const FieldDef& field, const JsonValue& value, long double low, long double high)
    {
        std::string digits;
        if (value.type == JsonValue::Type::String || value.type == JsonValue::Type::Number) {
            digits = value.text;
        } else {
            Error("protobuf:jsonError:fieldExpectsInteger", "field \"%s\" expects an integer.",
                field.name);
        }
        // Exponent forms such as 1e3 are allowed when they are whole numbers.
        T result { };
        auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result);
        if (parsed.ec == std::errc() && parsed.ptr == digits.data() + digits.size()) {
            return result;
        }
        char* end = nullptr;
        const long double number = std::strtold(digits.c_str(), &end);
        if (end == digits.c_str() || *end != '\0' || number != std::floor(number) || number < low
            || number > high) {
            Error("protobuf:jsonError:fieldExpectsIntegerRange",
                "field \"%s\" expects an integer in range, not \"%s\".", field.name, digits);
        }
        return (T)number;
    }
    //=============================================================================
    template <typename T>
    void
    store(Column& column, bool append, size_t row, T value)
    {
        if (append) {
            const size_t n = column.values.size();
            column.values.resize(n + sizeof(T));
            std::memcpy(column.values.data() + n, &value, sizeof(T));
        } else {
            std::memcpy(column.values.data() + row * sizeof(T), &value, sizeof(T));
        }
    }
    //=============================================================================
    double
    jsonReal(const FieldDef& field, const JsonValue& value)
    {
        if (value.type == JsonValue::Type::Number) {
            return value.number;
        }
        if (value.type == JsonValue::Type::String) {
            if (value.text == "NaN") {
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (value.text == "Infinity") {
                return std::numeric_limits<double>::infinity();
            }
            if (value.text == "-Infinity") {
                return -std::numeric_limits<double>::infinity();
            }
            char* end = nullptr;
            const double number = std::strtod(value.text.c_str(), &end);
            if (end != value.text.c_str() && *end == '\0') {
                return number;
            }
        }
        Error(
            "protobuf:jsonError:fieldExpectsNumber", "field \"%s\" expects a number.", field.name);
    }
    //=============================================================================
    void
    putScalar(const Schema& schema, const FieldDef& field, Column& column, bool append, size_t row,
        const JsonValue& value)
    {
        switch (field.type) {
        case FieldType::Double:
            store<double>(column, append, row, jsonReal(field, value));
            return;
        case FieldType::Float:
            store<float>(column, append, row, (float)jsonReal(field, value));
            return;
        case FieldType::Int64:
        case FieldType::SInt64:
        case FieldType::SFixed64:
            store<int64_t>(column, append, row,
                jsonInteger<int64_t>(
                    field, value, -9223372036854775808.0L, 9223372036854775807.0L));
            return;
        case FieldType::UInt64:
        case FieldType::Fixed64:
            store<uint64_t>(column, append, row,
                jsonInteger<uint64_t>(field, value, 0.0L, 18446744073709551615.0L));
            return;
        case FieldType::Int32:
        case FieldType::SInt32:
        case FieldType::SFixed32:
            store<int32_t>(column, append, row,
                jsonInteger<int32_t>(field, value, -2147483648.0L, 2147483647.0L));
            return;
        case FieldType::UInt32:
        case FieldType::Fixed32:
            store<uint32_t>(
                column, append, row, jsonInteger<uint32_t>(field, value, 0.0L, 4294967295.0L));
            return;
        case FieldType::Bool:
            if (value.type != JsonValue::Type::Bool) {
                Error("protobuf:jsonError:fieldExpectsTrueFalse",
                    "field \"%s\" expects true or false.", field.name);
            }
            store<uint8_t>(column, append, row, value.boolean ? 1 : 0);
            return;
        case FieldType::Enum: {
            int32_t number = 0;
            if (value.type == JsonValue::Type::String) {
                bool found = false;
                for (const EnumValueDef& v : schema.enums[(size_t)field.enumIndex].values) {
                    if (v.name == value.text) {
                        number = v.number;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    Error("protobuf:jsonError:notAnEnumValue",
                        "\"%s\" is not a value of enum \"%s\".", value.text,
                        schema.enums[(size_t)field.enumIndex].fullName);
                }
            } else if (value.type == JsonValue::Type::Null) {
                number = 0;
            } else {
                number = jsonInteger<int32_t>(field, value, -2147483648.0L, 2147483647.0L);
            }
            store<int32_t>(column, append, row, number);
            return;
        }
        case FieldType::String:
        case FieldType::Bytes: {
            if (value.type != JsonValue::Type::String) {
                Error("protobuf:jsonError:fieldExpectsString", "field \"%s\" expects a string.",
                    field.name);
            }
            std::string bytes = value.text;
            if (field.type == FieldType::Bytes && !base64Decode(value.text, bytes)) {
                Error("protobuf:jsonError:fieldExpectsBase64Text",
                    "field \"%s\" expects base64 text.", field.name);
            }
            if (append) {
                column.bytes.push_back(bytes);
            } else {
                column.bytes[row] = bytes;
            }
            return;
        }
        default:
            Error("protobuf:jsonError:fieldUnsupportedType",
                "field \"%s\" has an unsupported type.", field.name);
        }
    }
    //=============================================================================
    JsonValue
    JsonReader::document()
    {
        try {
            JsonValue result = value(0);
            skipSpace();
            if (pos != text.size()) {
                Error("protobuf:jsonError:unexpectedTextAfterJSONValue",
                    "unexpected text after the JSON value");
            }
            return result;
        } catch (const ProtoError& e) {
            Error("protobuf:jsonError:atCharacter", "%s at character %d.", e, pos + 1);
        }
    }
    //=============================================================================
    void
    JsonReader::skipSpace()
    {
        while (pos < text.size()
            && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
            pos++;
        }
    }

    //=============================================================================
    JsonValue
    JsonReader::value(int depth)
    {
        if (depth > 100) {
            Error("protobuf:jsonError:nestingTooDeep", "nesting too deep");
        }
        skipSpace();
        if (pos >= text.size()) {
            Error("protobuf:jsonError:unexpectedEndText", "unexpected end of text");
        }
        JsonValue result;
        const char c = text[pos];
        if (c == '{') {
            result.type = JsonValue::Type::Object;
            pos++;
            skipSpace();
            if (pos < text.size() && text[pos] == '}') {
                pos++;
                return result;
            }
            while (true) {
                skipSpace();
                if (pos >= text.size() || text[pos] != '"') {
                    Error("protobuf:jsonError:expectedMemberName", "expected a member name");
                }
                std::string name = stringLiteral();
                skipSpace();
                if (pos >= text.size() || text[pos] != ':') {
                    Error("protobuf:jsonError:expectedColon", "expected ':'");
                }
                pos++;
                result.members.emplace_back(name, value(depth + 1));
                skipSpace();
                if (pos < text.size() && text[pos] == ',') {
                    pos++;
                    continue;
                }
                if (pos < text.size() && text[pos] == '}') {
                    pos++;
                    return result;
                }
                Error("protobuf:jsonError:expectedCommaOrBrace", "expected ',' or '}'");
            }
        }
        if (c == '[') {
            result.type = JsonValue::Type::Array;
            pos++;
            skipSpace();
            if (pos < text.size() && text[pos] == ']') {
                pos++;
                return result;
            }
            while (true) {
                result.items.push_back(value(depth + 1));
                skipSpace();
                if (pos < text.size() && text[pos] == ',') {
                    pos++;
                    continue;
                }
                if (pos < text.size() && text[pos] == ']') {
                    pos++;
                    return result;
                }
                Error("protobuf:jsonError:expectedCommaOrBracket", "expected ',' or ']'");
            }
        }
        if (c == '"') {
            result.type = JsonValue::Type::String;
            result.text = stringLiteral();
            return result;
        }
        if (text.compare(pos, 4, "true") == 0) {
            result.type = JsonValue::Type::Bool;
            result.boolean = true;
            pos += 4;
            return result;
        }
        if (text.compare(pos, 5, "false") == 0) {
            result.type = JsonValue::Type::Bool;
            pos += 5;
            return result;
        }
        if (text.compare(pos, 4, "null") == 0) {
            pos += 4;
            return result;
        }
        const size_t start = pos;
        if (text[pos] == '-') {
            pos++;
        }
        while (pos < text.size()
            && ((text[pos] >= '0' && text[pos] <= '9') || text[pos] == '.' || text[pos] == 'e'
                || text[pos] == 'E' || text[pos] == '+' || text[pos] == '-')) {
            pos++;
        }
        if (pos == start) {
            Error("protobuf:jsonError:unexpectedCharacter", "unexpected character");
        }
        result.type = JsonValue::Type::Number;
        result.text = text.substr(start, pos - start);
        char* end = nullptr;
        result.number = std::strtod(result.text.c_str(), &end);
        if (end != result.text.c_str() + result.text.size()) {
            Error("protobuf:jsonError:invalidNumber", "invalid number");
        }
        return result;
    }
    //=============================================================================
    std::string
    JsonReader::stringLiteral()
    {
        pos++;
        std::string out;
        while (pos < text.size() && text[pos] != '"') {
            char c = text[pos++];
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos >= text.size()) {
                break;
            }
            char e = text[pos++];
            switch (e) {
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'u': {
                auto hex4 = [this](uint32_t& code) {
                    code = 0;
                    for (int k = 0; k < 4; ++k) {
                        if (pos >= text.size()) {
                            return false;
                        }
                        char h = text[pos++];
                        code <<= 4;
                        if (h >= '0' && h <= '9') {
                            code |= (uint32_t)(h - '0');
                        } else if (h >= 'a' && h <= 'f') {
                            code |= (uint32_t)(h - 'a' + 10);
                        } else if (h >= 'A' && h <= 'F') {
                            code |= (uint32_t)(h - 'A' + 10);
                        } else {
                            return false;
                        }
                    }
                    return true;
                };
                uint32_t code = 0;
                if (!hex4(code)) {
                    Error("protobuf:jsonError:invalidUEscape", "invalid \\u escape");
                }
                if (code >= 0xD800 && code <= 0xDBFF && pos + 1 < text.size() && text[pos] == '\\'
                    && text[pos + 1] == 'u') {
                    pos += 2;
                    uint32_t low = 0;
                    if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                        Error("protobuf:jsonError:invalidSurrogatePair", "invalid surrogate pair");
                    }
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                } else if (code >= 0xD800 && code <= 0xDFFF) {
                    // Half of a pair alone is not a character.
                    Error("protobuf:jsonError:invalidSurrogatePair", "invalid surrogate pair");
                }
                if (code < 0x80) {
                    out.push_back((char)code);
                } else if (code < 0x800) {
                    out.push_back((char)(0xC0 | (code >> 6)));
                    out.push_back((char)(0x80 | (code & 0x3F)));
                } else if (code < 0x10000) {
                    out.push_back((char)(0xE0 | (code >> 12)));
                    out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back((char)(0x80 | (code & 0x3F)));
                } else {
                    out.push_back((char)(0xF0 | (code >> 18)));
                    out.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
                    out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                    out.push_back((char)(0x80 | (code & 0x3F)));
                }
            } break;
            case '"':
            case '\\':
            case '/':
                out.push_back(e);
                break;
            default:
                // JSON allows no other escape.
                Error("protobuf:jsonError:invalidEscape", "invalid escape \%s", std::string(1, e));
            }
        }
        if (pos >= text.size()) {
            Error("protobuf:jsonError:unterminatedString", "unterminated string");
        }
        pos++;
        return out;
    }
    //=============================================================================
    bool
    isValidUtf8Text(const std::string& text)
    {
        size_t k = 0;
        while (k < text.size()) {
            const unsigned char c = (unsigned char)text[k];
            if (c < 0x80) {
                k++;
                continue;
            }
            const size_t extra = (c & 0xE0) == 0xC0 ? 1
                : (c & 0xF0) == 0xE0                ? 2
                : (c & 0xF8) == 0xF0                ? 3
                                                    : 0;
            if (extra == 0 || k + extra >= text.size()) {
                return false;
            }
            for (size_t j = 1; j <= extra; ++j) {
                if (((unsigned char)text[k + j] & 0xC0) != 0x80) {
                    return false;
                }
            }
            k += extra + 1;
        }
        return true;
    }
    //=============================================================================
    // Text format reader: tokens, then fields written straight into the columns.
    class TextReader
    {
    public:
        TextReader(const Codec& codec, const std::string& text) : codec(codec), text(text) { }
        // Reads the fields of one message up to close (0: the end of the text) as a new row.
        void
        readMessage(ColumnSet& set, char close, int depth);
        // Line of the reading position.
        size_t
        currentLine() const
        {
            return line;
        }
        void
        finish()
        {
            skipSpace();
            if (pos != text.size()) {
                Error("protobuf:textError:unexpected", "unexpected \"%s\"", text.substr(pos, 1));
            }
        }

    private:
        const Codec& codec;
        const std::string& text;
        size_t pos = 0;
        size_t line = 1;
        void
        skipSpace()
        {
            while (pos < text.size()) {
                const char c = text[pos];
                if (c == '\n') {
                    line++;
                    pos++;
                } else if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') {
                    pos++;
                } else if (c == '#') {
                    while (pos < text.size() && text[pos] != '\n') {
                        pos++;
                    }
                } else {
                    break;
                }
            }
        }
        bool
        accept(char c)
        {
            skipSpace();
            if (pos < text.size() && text[pos] == c) {
                pos++;
                return true;
            }
            return false;
        }
        void
        expect(char c)
        {
            if (!accept(c)) {
                Error("protobuf:textError:expectedCharacter", "expected \"%s\"", std::string(1, c));
            }
        }
        bool
        atIdentifier()
        {
            skipSpace();
            return pos < text.size()
                && (std::isalpha((unsigned char)text[pos]) || text[pos] == '_');
        }
        std::string
        identifier()
        {
            skipSpace();
            const size_t start = pos;
            while (pos < text.size()
                && (std::isalnum((unsigned char)text[pos]) || text[pos] == '_'
                    || text[pos] == '.')) {
                pos++;
            }
            if (pos == start) {
                Error("protobuf:textError:expectedName", "expected a name");
            }
            return text.substr(start, pos - start);
        }
        // Adjacent quoted strings, concatenated, escapes decoded.
        std::string
        strings()
        {
            std::string out;
            skipSpace();
            if (pos >= text.size() || (text[pos] != '"' && text[pos] != '\'')) {
                Error("protobuf:textError:expectedString", "expected a string");
            }
            while (pos < text.size() && (text[pos] == '"' || text[pos] == '\'')) {
                const char quote = text[pos++];
                while (pos < text.size() && text[pos] != quote) {
                    char c = text[pos++];
                    if (c == '\n') {
                        Error("protobuf:textError:stringClosed", "string not closed");
                    }
                    if (c != '\\') {
                        out.push_back(c);
                        continue;
                    }
                    if (pos >= text.size()) {
                        break;
                    }
                    c = text[pos++];
                    switch (c) {
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'a':
                        out.push_back('\a');
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'v':
                        out.push_back('\v');
                        break;
                    case 'x':
                    case 'X': {
                        int value = 0;
                        int digits = 0;
                        while (digits < 2 && pos < text.size()
                            && std::isxdigit((unsigned char)text[pos])) {
                            value = value * 16 + hexDigit(text[pos++]);
                            digits++;
                        }
                        if (digits == 0) {
                            Error("protobuf:textError:invalidXEscape", "invalid \\x escape");
                        }
                        out.push_back((char)value);
                    } break;
                    case 'u':
                    case 'U': {
                        const int count = c == 'u' ? 4 : 8;
                        uint32_t code = 0;
                        for (int k = 0; k < count; ++k) {
                            if (pos >= text.size() || !std::isxdigit((unsigned char)text[pos])) {
                                Error("protobuf:textError:invalidUnicodeEscape",
                                    "invalid unicode escape");
                            }
                            code = code * 16 + (uint32_t)hexDigit(text[pos++]);
                        }
                        appendUtf8(out, code);
                    } break;
                    default:
                        if (c >= '0' && c <= '7') {
                            int value = c - '0';
                            for (int k = 0;
                                k < 2 && pos < text.size() && text[pos] >= '0' && text[pos] <= '7';
                                ++k) {
                                value = value * 8 + (text[pos++] - '0');
                            }
                            out.push_back((char)value);
                        } else {
                            out.push_back(c);
                        }
                    }
                }
                if (pos >= text.size()) {
                    Error("protobuf:textError:stringClosed", "string not closed");
                }
                pos++;
                skipSpace();
            }
            return out;
        }
        static int
        hexDigit(char c)
        {
            return c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10;
        }
        static void
        appendUtf8(std::string& out, uint32_t code)
        {
            if (code < 0x80) {
                out.push_back((char)code);
            } else if (code < 0x800) {
                out.push_back((char)(0xC0 | (code >> 6)));
                out.push_back((char)(0x80 | (code & 0x3F)));
            } else if (code < 0x10000) {
                out.push_back((char)(0xE0 | (code >> 12)));
                out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (code & 0x3F)));
            } else {
                out.push_back((char)(0xF0 | (code >> 18)));
                out.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
                out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
                out.push_back((char)(0x80 | (code & 0x3F)));
            }
        }
        // A scalar token: number, identifier or quoted strings (with an optional '-').
        std::string
        scalarToken(bool& quoted)
        {
            skipSpace();
            quoted = pos < text.size() && (text[pos] == '"' || text[pos] == '\'');
            if (quoted) {
                return strings();
            }
            const size_t start = pos;
            if (pos < text.size() && text[pos] == '-') {
                pos++;
                skipSpace();
            }
            while (pos < text.size()
                && (std::isalnum((unsigned char)text[pos]) || text[pos] == '.' || text[pos] == '_'
                    || ((text[pos] == '+' || text[pos] == '-')
                        && (text[pos - 1] == 'e' || text[pos - 1] == 'E')))) {
                pos++;
            }
            if (pos == start) {
                Error("protobuf:textError:expectedValue", "expected a value");
            }
            std::string token = text.substr(start, pos - start);
            token.erase(std::remove(token.begin(), token.end(), ' '), token.end());
            return token;
        }
        void
        value(ColumnSet& set, size_t row, int32_t fieldIndex, bool append);
        void
        field(ColumnSet& set, size_t row, int depth);
        // "[url] { ... }" in an Any: the message is encoded into its value field.
        void
        anyField(ColumnSet& set, size_t row, int depth)
        {
            const Schema& schema = codec.schema();
            if (specialOf(schema, set.messageIndex) != Special::Any) {
                Error("protobuf:textError:allowedGoogleProtobufAnyMessage",
                    "\"[\" is only allowed in a google.protobuf.Any message");
            }
            skipSpace();
            const size_t start = pos;
            while (pos < text.size() && text[pos] != ']') {
                pos++;
            }
            std::string url = text.substr(start, pos - start);
            url.erase(std::remove_if(
                          url.begin(), url.end(), [](unsigned char c) { return std::isspace(c); }),
                url.end());
            expect(']');
            const int32_t type = anyType(schema, url);
            if (type < 0) {
                Error("protobuf:textError:typeSchema", "type of \"%s\" is not in the schema", url);
            }
            accept(':');
            skipSpace();
            const char open = pos < text.size() ? text[pos] : 0;
            if (open != '{' && open != '<') {
                Error("protobuf:textError:expectedAfter", "expected \"{\" after \"[%s]\"", url);
            }
            pos++;
            ColumnSet inner = codec.createColumnSet(type);
            readMessage(inner, open == '{' ? '}' : '>', depth + 1);
            codec.finish(inner);
            set.columns[0].bytes[row] = url;
            set.columns[1].bytes[row] = codec.encodeRow(inner, 0);
        }
    };
    //=============================================================================
    void
    TextReader::readMessage(ColumnSet& set, char close, int depth)
    {
        if (depth > 100) {
            Error("protobuf:textError:nestingTooDeep", "nesting too deep");
        }
        codec.beginRow(set);
        const size_t row = set.rows - 1;
        while (true) {
            skipSpace();
            if (pos >= text.size() || (close != 0 && text[pos] == close)) {
                break;
            }
            field(set, row, depth);
            if (!accept(';')) {
                accept(',');
            }
        }
        if (close != 0) {
            expect(close);
        }
        codec.endRow(set);
    }
    //=============================================================================
    void
    TextReader::field(ColumnSet& set, size_t row, int depth)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        int32_t fieldIndex = -1;
        std::string name;
        if (accept('[')) {
            if (specialOf(schema, set.messageIndex) == Special::Any) {
                anyField(set, row, depth);
                return;
            }
            // Extension: [full.name].
            skipSpace();
            const size_t start = pos;
            while (pos < text.size() && text[pos] != ']') {
                pos++;
            }
            name = text.substr(start, pos - start);
            name.erase(std::remove_if(name.begin(), name.end(),
                           [](unsigned char c) { return std::isspace(c); }),
                name.end());
            expect(']');
            if (!name.empty() && name[0] == '.') {
                name.erase(0, 1);
            }
            for (size_t k = 0; fieldIndex < 0 && k < message.fields.size(); ++k) {
                if (message.fields[k].extension == name) {
                    fieldIndex = (int32_t)k;
                }
            }
            if (fieldIndex < 0) {
                Error("protobuf:textError:unknownExtensionMessage",
                    "unknown extension \"%s\" for message \"%s\"", name, message.fullName);
            }
        } else {
            name = identifier();
            fieldIndex = message.findFieldByName(name);
            for (size_t k = 0; fieldIndex < 0 && k < message.fields.size(); ++k) {
                if (message.fields[k].extension.empty()
                    && textName(schema, message, message.fields[k]) == name) {
                    fieldIndex = (int32_t)k;
                }
            }
        }
        if (fieldIndex < 0) {
            Error("protobuf:textError:unknownFieldMessage",
                "unknown field \"%s\" for message \"%s\"", name, message.fullName);
        }
        const FieldDef& field = message.fields[(size_t)fieldIndex];
        Column& column = set.columns[(size_t)fieldIndex];
        const bool repeated = field.label == FieldLabel::Repeated;
        if (field.kind == ValueKind::Message) {
            accept(':');
            auto one = [&]() {
                skipSpace();
                const char open = pos < text.size() ? text[pos] : 0;
                if (open != '{' && open != '<') {
                    Error("protobuf:textError:expectedField", "expected \"{\" for field \"%s\"",
                        name);
                }
                pos++;
                const char close = open == '{' ? '}' : '>';
                ColumnSet& child = codec.childSet(column, field.messageIndex);
                readMessage(child, close, depth + 1);
                if (!repeated) {
                    if (column.childRow[row] >= 0) {
                        Error("protobuf:textError:fieldGivenMoreOnce",
                            "field \"%s\" is given more than once", name);
                    }
                    column.childRow[row] = (int64_t)child.rows - 1;
                }
            };
            if (repeated && accept('[')) {
                if (!accept(']')) {
                    do {
                        one();
                    } while (accept(','));
                    expect(']');
                }
            } else {
                one();
            }
        } else {
            expect(':');
            if (repeated && accept('[')) {
                if (!accept(']')) {
                    do {
                        value(set, row, fieldIndex, true);
                    } while (accept(','));
                    expect(']');
                }
            } else {
                value(set, row, fieldIndex, repeated);
            }
        }
        if (!repeated) {
            if (field.oneofIndex >= 0 && !message.oneofs[(size_t)field.oneofIndex].synthetic) {
                for (size_t k = 0; k < set.oneofIndices.size(); ++k) {
                    if (set.oneofIndices[k] == field.oneofIndex) {
                        if (set.oneofCase[k][row] >= 0 && set.oneofCase[k][row] != fieldIndex) {
                            Error("protobuf:textError:severalMembersOneofSet",
                                "several members of oneof \"%s\" are set",
                                message.oneofs[(size_t)field.oneofIndex].name);
                        }
                        set.oneofCase[k][row] = fieldIndex;
                    }
                }
            }
            if (column.hasPresence && field.kind != ValueKind::Message) {
                column.presence[row] = 1;
            }
        }
    }
    //=============================================================================
    void
    TextReader::value(ColumnSet& set, size_t row, int32_t fieldIndex, bool append)
    {
        const Schema& schema = codec.schema();
        const FieldDef& field
            = schema.messages[(size_t)set.messageIndex].fields[(size_t)fieldIndex];
        Column& column = set.columns[(size_t)fieldIndex];
        bool quoted = false;
        std::string token = scalarToken(quoted);
        JsonValue v;
        if (field.kind == ValueKind::Bytes) {
            if (!quoted) {
                Error("protobuf:textError:fieldExpectsString", "field \"%s\" expects a string",
                    field.name);
            }
            if (field.type == FieldType::String && !isValidUtf8Text(token)) {
                Error("protobuf:textError:stringFieldContainsInvalidUTF",
                    "string field \"%s\" contains invalid UTF-8", field.name);
            }
            if (append) {
                column.bytes.push_back(token);
            } else {
                column.bytes[row] = token;
            }
            return;
        }
        if (quoted) {
            Error("protobuf:textError:fieldExpectsNumberNameString",
                "field \"%s\" expects a number or a name, not a string", field.name);
        }
        std::string lower = token;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        if (field.type == FieldType::Bool) {
            v.type = JsonValue::Type::Bool;
            if (lower == "true" || lower == "t" || lower == "1") {
                v.boolean = true;
            } else if (!(lower == "false" || lower == "f" || lower == "0")) {
                Error("protobuf:textError:fieldExpectsTrueFalse",
                    "field \"%s\" expects true or false", field.name);
            }
        } else if (field.type == FieldType::Enum) {
            if (std::isalpha((unsigned char)token[0]) || token[0] == '_') {
                v.type = JsonValue::Type::String;
                v.text = token;
            } else {
                v.type = JsonValue::Type::Number;
                v.text = token;
                v.number = std::strtod(token.c_str(), nullptr);
            }
        } else if (field.kind == ValueKind::Float || field.kind == ValueKind::Double) {
            const bool negative = lower[0] == '-';
            const std::string body = negative ? lower.substr(1) : lower;
            v.type = JsonValue::Type::Number;
            if (body == "inf" || body == "infinity") {
                v.number = negative ? -std::numeric_limits<double>::infinity()
                                    : std::numeric_limits<double>::infinity();
            } else if (body == "nan") {
                v.number = std::numeric_limits<double>::quiet_NaN();
            } else {
                std::string digits = lower;
                if (!digits.empty() && (digits.back() == 'f')) {
                    digits.pop_back();
                }
                char* end = nullptr;
                v.number
                    = (digits.size() > 2
                          && (digits.compare(0, 2, "0x") == 0 || digits.compare(0, 3, "-0x") == 0))
                    ? (double)std::strtoll(digits.c_str(), &end, 16)
                    : std::strtod(digits.c_str(), &end);
                if (end == nullptr || *end != '\0') {
                    Error("protobuf:textError:invalidNumber", "invalid number \"%s\"", token);
                }
            }
        } else {
            // Integers: decimal, hexadecimal (0x) or octal (leading 0).
            const bool negative = token[0] == '-';
            const std::string body = negative ? token.substr(1) : token;
            int base = 10;
            size_t skip = 0;
            if (body.size() > 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X')) {
                base = 16;
                skip = 2;
            } else if (body.size() > 1 && body[0] == '0') {
                base = 8;
                skip = 1;
            }
            uint64_t magnitude = 0;
            auto parsed
                = std::from_chars(body.data() + skip, body.data() + body.size(), magnitude, base);
            if (parsed.ec != std::errc() || parsed.ptr != body.data() + body.size()) {
                Error("protobuf:textError:invalidInteger", "invalid integer \"%s\"", token);
            }
            v.type = JsonValue::Type::String;
            v.text = negative ? "-" + std::to_string(magnitude) : std::to_string(magnitude);
        }
        putScalar(schema, field, column, append, row, v);
    }
    //=============================================================================
} // namespace
//=============================================================================
size_t
parseText(const Codec& codec, ColumnSet& set, const std::string& text)
{
    const size_t before = set.rows;
    TextReader reader(codec, text);
    try {
        try {
            reader.readMessage(set, 0, 0);
            reader.finish();
        } catch (const ProtoError& e) {
            // Errors of the reader name the line.
            if (e.id().rfind("protobuf:textError:", 0) == 0) {
                Error("protobuf:textError:atLine", "text format: line %d: %s.",
                    reader.currentLine(), e);
            }
            throw;
        }
    } catch (...) {
        codec.truncate(set, before);
        throw;
    }
    codec.finish(set);
    return set.rows - before;
}
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
