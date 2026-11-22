//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtobufConversion.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
using namespace Nelson::Protobuf;
//=============================================================================
namespace {
    //=============================================================================
    // Adds "a" (the whole field) or "a.b.c" (a field of sub-messages) to a selection.
    void
    addFieldPath(const Schema& schema, int32_t messageIndex, FieldSelection& selection,
        const std::string& path);
    //=============================================================================
    // google.protobuf types converted to Nelson values: Timestamp to datetime (UTC), Duration to
    // duration, wrappers (Int32Value...) to their value.
    enum class WellKnown
    {
        None,
        Timestamp,
        Duration,
        Wrapper,
        Any
    };
    WellKnown
    wellKnownOf(const Schema& schema, int32_t messageIndex);
    // Appends one Any row from a struct with TypeUrl and Message (or Value); returns the row.
    int64_t
    packAny(const Codec& codec, ColumnSet& child, const FieldDef& field, const ArrayOf& value);
    // Seconds and nanoseconds of a Timestamp or Duration row.
    void
    secondsAndNanos(const ColumnSet& set, size_t row, int64_t& seconds, int32_t& nanos);
    ArrayOf
    timeValues(WellKnown kind, const std::vector<int64_t>& seconds,
        const std::vector<int32_t>& nanos, const std::vector<uint8_t>& present,
        const Dimensions& dims);
    // Rows appended to a Timestamp, Duration or wrapper child set from a Nelson value; the index of
    // the last row, -1 when no row is added (NaT, NaN duration).
    int64_t
    appendWellKnown(const Codec& codec, ColumnSet& child, WellKnown kind, const FieldDef& field,
        const ArrayOf& value, bool repeated);
    // Seconds and nanoseconds of the elements of a datetime (UTC or no zone) or duration value;
    // valid is 0 for NaT and NaN.
    void
    timeParts(WellKnown kind, const FieldDef& field, const ArrayOf& value,
        std::vector<int64_t>& seconds, std::vector<int32_t>& nanos, std::vector<uint8_t>& valid);
    // Rows of a struct of Nx1 columns; returns, per row, whether it holds a value. parentRows
    // is the row count of the parent columns (SIZE_MAX at the top): the columns of a
    // sub-message must have as many rows, and a message without fields takes them.
    std::vector<uint8_t>
    appendColumnRows(const Codec& codec, ColumnSet& set, const ArrayOf& columns,
        const ArrayOf& presence, size_t parentRows = SIZE_MAX);
    //=============================================================================
    // Values shared by every row of one conversion: built once, then copied (shared storage).
    class ConversionCache
    {
    public:
        ConversionCache(const Codec& codec, bool wellKnownTypes, bool keepUnknown);
        const Codec& codec;
        const Schema& schema;
        const bool wellKnownTypes;
        // Structs and columns get an UnknownFields field (wire bytes of unknown fields).
        const bool keepUnknown;
        // Absent scalar fields give their default value instead of [] or a missing value.
        bool fillDefaults = false;
        // Enum values as their names.
        bool enumsAsText = false;
        // Name of an enum value, or the number as text when the enum has no such value.
        ArrayOf
        enumName(const FieldDef& field, int32_t number) const
        {
            for (const EnumValueDef& v : schema.enums[(size_t)field.enumIndex].values) {
                if (v.number == number) {
                    return ArrayOf::characterArray(v.name);
                }
            }
            return ArrayOf::characterArray(std::to_string(number));
        }
        // Conversion of a message type (None when wellKnownTypes is false).
        WellKnown
        wellKnown(int32_t messageIndex)
        {
            return wellKnownTypes ? message(messageIndex).wellKnown : WellKnown::None;
        }
        // Struct field names of a message: its fields, then its real oneofs.
        const stringVector&
        names(int32_t messageIndex);
        // 0x0 struct with the message fields (absent sub-message).
        const ArrayOf&
        absentMessage(int32_t messageIndex);
        // Value of a repeated field with no element in a row.
        const ArrayOf&
        emptyRepeated(int32_t messageIndex, size_t fieldIndex);
        // Name of the set member of a oneof as char, '' for fieldIndex -1.
        const ArrayOf&
        memberName(int32_t messageIndex, int32_t fieldIndex);
        // [] for an absent value, '' for an absent text in a column.
        const ArrayOf absent;
        const ArrayOf emptyText;

    private:
        struct PerMessage
        {
            bool ready = false;
            stringVector names;
            ArrayOf absentMessage;
            std::vector<ArrayOf> emptyRepeated;
            std::vector<ArrayOf> memberNames;
            WellKnown wellKnown = WellKnown::None;
        };
        std::vector<PerMessage> messages;
        PerMessage&
        message(int32_t messageIndex);
    };
    //=============================================================================
    NelsonType
    nelsonTypeOf(ValueKind kind);
    // Copies a double or single column into slots of kind (rounded and saturated like int32(),
    // NaN as 0); false for any other column class.
    bool
    convertFloatingColumn(const ArrayOf& column, ValueKind kind, uint8_t* out, size_t rows);
    ArrayOf
    scalarValue(ValueKind kind, const uint8_t* slot);
    ArrayOf
    numericArray(ValueKind kind, const uint8_t* source, size_t count, const Dimensions& dims);
    ArrayOf
    textValue(const FieldDef& field, const std::string& text);
    ArrayOf
    emptyRepeatedValue(const Schema& schema, const FieldDef& field, bool unknown);
    ArrayOf
    emptyStructOf(const Schema& schema, int32_t messageIndex, indexType rows, indexType columns,
        bool unknown);
    // Fields, real oneofs, then UnknownFields when unknown is true (and no field has that name).
    stringVector
    structFieldNames(const MessageDef& message, bool unknown);
    // Wire bytes of the unknown fields of a row, as a uint8 row vector.
    ArrayOf
    unknownValue(const ColumnSet& set, size_t row);
    const char* const unknownFieldName = "UnknownFields";
    ArrayOf
    messageRows(ConversionCache& cache, const ColumnSet& set, size_t first, size_t count);
    // Any as a struct with TypeUrl, Value (packed bytes) and Message (the packed message decoded
    // when its type is in the schema, else []).
    ArrayOf
    anyValue(ConversionCache& cache, const ColumnSet& set, size_t row);
    ArrayOf
    selectedMessageRows(ConversionCache& cache, const ColumnSet& set, size_t first, size_t count);
    ArrayOf
    fieldValueAt(ConversionCache& cache, const ColumnSet& set, size_t fieldIndex, size_t row);
    // Column values of the rows given by map (all rows without map), missing ones as NaN or 0.
    void
    gatherNative(ValueKind kind, const uint8_t* source, const uint8_t* flags, const int64_t* map,
        size_t rows, uint8_t* out, logical* present);
    void
    gatherAsDouble(ValueKind kind, const uint8_t* source, const uint8_t* flags, const int64_t* map,
        size_t rows, double* out, logical* present);
    ArrayOf
    messageColumns(ConversionCache& cache, const ColumnSet* set, int32_t messageIndex,
        const std::vector<int64_t>* rowMap, size_t rows, bool mayBeMissing, bool missingAsNaN,
        std::vector<int32_t>& stack, ArrayOf& presence);
    // Nx1 column of a singular well-known type field, with its logical presence column.
    ArrayOf
    wellKnownColumn(ConversionCache& cache, const ColumnSet* set, const Column* column,
        const FieldDef& field, const std::vector<int64_t>* rowMap, size_t rows, bool missingAsNaN,
        stringVector& presenceNames, ArrayOfVector& presenceValues);
    void
    appendRecords(const Codec& codec, ColumnSet& set, const ArrayOf& value);
    void
    assignField(
        const Codec& codec, ColumnSet& set, size_t fieldIndex, size_t row, const ArrayOf& value);
    std::string
    bytesFromValue(const FieldDef& field, const ArrayOf& value);
    std::vector<uint8_t>
    numericPayload(const Schema& schema, const FieldDef& field, const ArrayOf& value);
    // Name of the flag that tells, in the presence of a sub-message, which records have it:
    // "Present", with underscores added when the message has a field of that name.
    std::string
    presentFlagName(const MessageDef& message);
    //=============================================================================
} // namespace
//=============================================================================
ArrayOf
rowsToStructArray(const Codec& codec, const ColumnSet& set, bool wellKnownTypes, bool fillDefaults,
    bool enumsAsText)
{
    ConversionCache cache(codec, wellKnownTypes, set.keepUnknown);
    cache.fillDefaults = fillDefaults;
    cache.enumsAsText = enumsAsText;
    return messageRows(cache, set, 0, set.rows);
}
//=============================================================================
ArrayOf
rowsToColumns(const Codec& codec, const ColumnSet& set, bool missingAsNaN, bool wellKnownTypes,
    ArrayOf& presence, bool fillDefaults, bool enumsAsText)
{
    ConversionCache cache(codec, wellKnownTypes, set.keepUnknown);
    cache.fillDefaults = fillDefaults;
    cache.enumsAsText = enumsAsText;
    std::vector<int32_t> stack { set.messageIndex };
    return messageColumns(
        cache, &set, set.messageIndex, nullptr, set.rows, false, missingAsNaN, stack, presence);
}
//=============================================================================
void
appendColumns(const Codec& codec, ColumnSet& set, const ArrayOf& columns, const ArrayOf& presence)
{
    if (!columns.isStruct() || !columns.isScalar()) {
        Nelson::Error("protobuf:invalidValue:columnsLayoutNeedsScalarStruct",
            "the columns layout needs a scalar struct of Nx1 columns.");
    }
    appendColumnRows(codec, set, columns, presence);
    codec.finish(set);
}
//=============================================================================
void
appendStructRows(const Codec& codec, ColumnSet& set, const ArrayOf& value)
{
    if (!value.isStruct()) {
        Nelson::Error("protobuf:invalidValue:messageValuesGivenStruct",
            "message values must be given as a struct.");
    }
    appendRecords(codec, set, value);
    codec.finish(set);
}
//=============================================================================
DecodeSettings
decodeSettings(const ArrayOf& options)
{
    DecodeSettings settings;
    settings.delimited = options.field("Delimited").asLogicalScalar();
    settings.columns = options.field("Layout").asUtf8String() == "columns";
    settings.skipInvalid = options.field("Skip").asLogicalScalar();
    settings.missingAsNaN = options.field("MissingAsNaN").asLogicalScalar();
    settings.fillDefaults
        = options.isField("FillDefaults") && options.field("FillDefaults").asLogicalScalar();
    settings.enumsAsText
        = options.isField("EnumsAsText") && options.field("EnumsAsText").asLogicalScalar();
    settings.wellKnownTypes = options.field("WellKnownTypes").asLogicalScalar();
    settings.keepUnknown = options.field("KeepUnknown").asLogicalScalar();
    settings.fields = options.field("Fields");
    settings.records = options.field("Records");
    settings.workers = (size_t)options.field("Workers").asDoubleScalar();
    return settings;
}
//=============================================================================
ArrayOf
rowsToValue(
    const Codec& codec, const ColumnSet& set, const DecodeSettings& settings, ArrayOf& presence)
{
    presence = ArrayOf::emptyStruct();
    if (settings.columns) {
        return rowsToColumns(codec, set, settings.missingAsNaN, settings.wellKnownTypes, presence,
            settings.fillDefaults, settings.enumsAsText);
    }
    return rowsToStructArray(
        codec, set, settings.wellKnownTypes, settings.fillDefaults, settings.enumsAsText);
}
//=============================================================================
std::shared_ptr<const FieldSelection>
fieldSelection(const Schema& schema, int32_t messageIndex, const ArrayOf& names)
{
    if (names.isEmpty()) {
        return nullptr;
    }
    auto selection = std::make_shared<FieldSelection>();
    for (const std::string& path : names.asUtf8StringVector(false)) {
        addFieldPath(schema, messageIndex, *selection, path);
    }
    return selection;
}
//=============================================================================
ArrayOf
decodeReport(const std::string& typeName, size_t records, size_t skipped, int64_t pending)
{
    stringVector names = { "Type", "Records", "Skipped" };
    ArrayOfVector values;
    values.push_back(ArrayOf::characterArray(typeName));
    values.push_back(ArrayOf::doubleScalar((double)records));
    values.push_back(ArrayOf::doubleScalar((double)skipped));
    if (pending >= 0) {
        names.push_back("PendingBytes");
        values.push_back(ArrayOf::doubleScalar((double)pending));
    }
    return ArrayOf::structScalar(names, values);
}
//=============================================================================
namespace {
    //=============================================================================
    NelsonType
    nelsonTypeOf(ValueKind kind)
    {
        switch (kind) {
        case ValueKind::Double:
            return NLS_DOUBLE;
        case ValueKind::Float:
            return NLS_SINGLE;
        case ValueKind::Int64:
            return NLS_INT64;
        case ValueKind::UInt64:
            return NLS_UINT64;
        case ValueKind::Int32:
            return NLS_INT32;
        case ValueKind::UInt32:
            return NLS_UINT32;
        case ValueKind::Bool:
            return NLS_LOGICAL;
        default:
            return NLS_UINT8;
        }
    }
    //=============================================================================
    ArrayOf
    numericArray(ValueKind kind, const uint8_t* source, size_t count, const Dimensions& dims)
    {
        NelsonType type = nelsonTypeOf(kind);
        void* data = ArrayOf::allocateArrayOf(type, (indexType)count, stringVector(), false);
        if (count > 0 && source != nullptr) {
            std::memcpy(data, source, count * valueSize(kind));
        }
        return ArrayOf(type, dims, data);
    }
    //=============================================================================
    ArrayOf
    textValue(const FieldDef& field, const std::string& text)
    {
        if (field.type == FieldType::String) {
            if (text.empty()) {
                return ArrayOf::characterArray(std::string());
            }
            return ArrayOf::characterArray(text);
        }
        void* data
            = ArrayOf::allocateArrayOf(NLS_UINT8, (indexType)text.size(), stringVector(), false);
        if (!text.empty()) {
            std::memcpy(data, text.data(), text.size());
        }
        return ArrayOf(NLS_UINT8, Dimensions(text.empty() ? 0 : 1, (indexType)text.size()), data);
    }
    //=============================================================================
    stringVector
    structFieldNames(const MessageDef& message, bool unknown)
    {
        stringVector names;
        for (const FieldDef& f : message.fields) {
            names.push_back(f.name);
        }
        for (const OneofDef& o : message.oneofs) {
            if (!o.synthetic) {
                names.push_back(o.name);
            }
        }
        if (unknown && message.findFieldByName(unknownFieldName) < 0) {
            names.push_back(unknownFieldName);
        }
        return names;
    }
    //=============================================================================
    ArrayOf
    emptyStructOf(
        const Schema& schema, int32_t messageIndex, indexType rows, indexType columns, bool unknown)
    {
        stringVector names = structFieldNames(schema.messages[(size_t)messageIndex], unknown);
        Dimensions dims(rows, columns);
        return ArrayOf::emptyStruct(names, dims);
    }
    //=============================================================================
    ArrayOf
    emptyRepeatedValue(const Schema& schema, const FieldDef& field, bool unknown)
    {
        switch (field.kind) {
        case ValueKind::Message:
            return emptyStructOf(schema, field.messageIndex, 1, 0, unknown);
        case ValueKind::Bytes:
            return ArrayOf::emptyCell(Dimensions(1, 0));
        default:
            return numericArray(field.kind, nullptr, 0, Dimensions(1, 0));
        }
    }
    //=============================================================================
    ArrayOf
    messageRows(ConversionCache& cache, const ColumnSet& set, size_t first, size_t count)
    {
        if (!set.keep.empty()) {
            return selectedMessageRows(cache, set, first, count);
        }
        const MessageDef& message = cache.schema.messages[(size_t)set.messageIndex];
        const stringVector& names = cache.names(set.messageIndex);
        if (count == 0) {
            return emptyStructOf(cache.schema, set.messageIndex, 1, 0, cache.keepUnknown);
        }
        const size_t fieldCount = names.size();
        ArrayOf* elements = static_cast<ArrayOf*>(
            ArrayOf::allocateArrayOf(NLS_STRUCT_ARRAY, (indexType)count, names, false));
        for (size_t j = 0; j < count; ++j) {
            size_t row = first + j;
            ArrayOf* element = elements + j * fieldCount;
            for (size_t k = 0; k < message.fields.size(); ++k) {
                element[k] = fieldValueAt(cache, set, k, row);
            }
            for (size_t k = 0; k < set.oneofCase.size(); ++k) {
                element[message.fields.size() + k]
                    = cache.memberName(set.messageIndex, set.oneofCase[k][row]);
            }
            if (fieldCount > message.fields.size() + set.oneofCase.size()) {
                element[fieldCount - 1] = unknownValue(set, row);
            }
        }
        return ArrayOf(NLS_STRUCT_ARRAY, Dimensions(1, (indexType)count), elements, false, names);
    }
    //=============================================================================
    // Rows of a 'Fields' selection: the kept fields, and the oneofs of kept members.
    ArrayOf
    selectedMessageRows(ConversionCache& cache, const ColumnSet& set, size_t first, size_t count)
    {
        const MessageDef& message = cache.schema.messages[(size_t)set.messageIndex];
        std::vector<size_t> fieldList;
        std::vector<size_t> oneofList;
        stringVector names;
        for (size_t k = 0; k < message.fields.size(); ++k) {
            if (set.keep[k] != 0) {
                fieldList.push_back(k);
                names.push_back(message.fields[k].name);
            }
        }
        for (size_t k = 0; k < set.oneofIndices.size(); ++k) {
            for (size_t f : fieldList) {
                if (message.fields[f].oneofIndex == set.oneofIndices[k]) {
                    oneofList.push_back(k);
                    names.push_back(message.oneofs[(size_t)set.oneofIndices[k]].name);
                    break;
                }
            }
        }
        const bool unknown = cache.keepUnknown && message.findFieldByName(unknownFieldName) < 0;
        if (unknown) {
            names.push_back(unknownFieldName);
        }
        if (count == 0) {
            Dimensions dims(1, 0);
            return ArrayOf::emptyStruct(names, dims);
        }
        ArrayOf* elements = static_cast<ArrayOf*>(
            ArrayOf::allocateArrayOf(NLS_STRUCT_ARRAY, (indexType)count, names, false));
        for (size_t j = 0; j < count; ++j) {
            size_t row = first + j;
            ArrayOf* element = elements + j * names.size();
            size_t slot = 0;
            for (size_t f : fieldList) {
                element[slot++] = fieldValueAt(cache, set, f, row);
            }
            for (size_t o : oneofList) {
                element[slot++] = cache.memberName(set.messageIndex, set.oneofCase[o][row]);
            }
            if (unknown) {
                element[slot++] = unknownValue(set, row);
            }
        }
        return ArrayOf(NLS_STRUCT_ARRAY, Dimensions(1, (indexType)count), elements, false, names);
    }
    //=============================================================================
    ArrayOf
    fieldValueAt(ConversionCache& cache, const ColumnSet& set, size_t fieldIndex, size_t row)
    {
        const FieldDef& field = cache.schema.messages[(size_t)set.messageIndex].fields[fieldIndex];
        const Column& column = set.columns[fieldIndex];
        if (column.repeated) {
            size_t first = (size_t)column.offsets[row];
            size_t count = (size_t)column.offsets[row + 1] - first;
            if (count == 0) {
                return cache.emptyRepeated(set.messageIndex, fieldIndex);
            }
            if (column.kind == ValueKind::Message) {
                const WellKnown kind = cache.wellKnown(field.messageIndex);
                if (kind == WellKnown::Any) {
                    stringVector names = { "TypeUrl", "Value", "Message" };
                    ArrayOf* elements = static_cast<ArrayOf*>(
                        ArrayOf::allocateArrayOf(NLS_STRUCT_ARRAY, (indexType)count, names, false));
                    for (size_t k = 0; k < count; ++k) {
                        ArrayOf one = anyValue(cache, *column.child, first + k);
                        for (size_t f = 0; f < names.size(); ++f) {
                            elements[k * names.size() + f] = one.field(names[f]);
                        }
                    }
                    return ArrayOf(
                        NLS_STRUCT_ARRAY, Dimensions(1, (indexType)count), elements, false, names);
                }
                if (kind == WellKnown::Timestamp || kind == WellKnown::Duration) {
                    std::vector<int64_t> seconds(count);
                    std::vector<int32_t> nanos(count);
                    for (size_t k = 0; k < count; ++k) {
                        secondsAndNanos(*column.child, first + k, seconds[k], nanos[k]);
                    }
                    return timeValues(kind, seconds, nanos, std::vector<uint8_t>(count, 1),
                        Dimensions(1, (indexType)count));
                }
                if (kind == WellKnown::Wrapper) {
                    const Column& inner = column.child->columns[0];
                    if (inner.kind == ValueKind::Bytes) {
                        const FieldDef& valueField
                            = cache.schema.messages[(size_t)field.messageIndex].fields[0];
                        ArrayOf* cells = static_cast<ArrayOf*>(ArrayOf::allocateArrayOf(
                            NLS_CELL_ARRAY, (indexType)count, stringVector(), false));
                        for (size_t k = 0; k < count; ++k) {
                            cells[k] = textValue(valueField, inner.bytes[first + k]);
                        }
                        return ArrayOf(NLS_CELL_ARRAY, Dimensions(1, (indexType)count), cells);
                    }
                    return numericArray(inner.kind,
                        inner.values.data() + first * valueSize(inner.kind), count,
                        Dimensions(1, (indexType)count));
                }
                return messageRows(cache, *column.child, first, count);
            }
            if (column.kind == ValueKind::Bytes) {
                ArrayOf* cells = static_cast<ArrayOf*>(ArrayOf::allocateArrayOf(
                    NLS_CELL_ARRAY, (indexType)count, stringVector(), false));
                for (size_t k = 0; k < count; ++k) {
                    cells[k] = textValue(field, column.bytes[first + k]);
                }
                return ArrayOf(NLS_CELL_ARRAY, Dimensions(1, (indexType)count), cells);
            }
            if (cache.enumsAsText && field.type == FieldType::Enum) {
                ArrayOf* cells = static_cast<ArrayOf*>(ArrayOf::allocateArrayOf(
                    NLS_CELL_ARRAY, (indexType)count, stringVector(), false));
                for (size_t k = 0; k < count; ++k) {
                    int32_t number;
                    std::memcpy(&number, column.values.data() + (first + k) * 4, 4);
                    cells[k] = cache.enumName(field, number);
                }
                return ArrayOf(NLS_CELL_ARRAY, Dimensions(1, (indexType)count), cells);
            }
            if (count == 1) {
                return scalarValue(
                    column.kind, column.values.data() + first * valueSize(column.kind));
            }
            return numericArray(column.kind, column.values.data() + first * valueSize(column.kind),
                count, Dimensions(1, (indexType)count));
        }
        if (column.kind == ValueKind::Message) {
            int64_t childRow = column.childRow[row];
            const WellKnown kind = cache.wellKnown(field.messageIndex);
            if (kind != WellKnown::None) {
                if (childRow < 0) {
                    return cache.absent;
                }
                if (kind == WellKnown::Wrapper) {
                    return fieldValueAt(cache, *column.child, 0, (size_t)childRow);
                }
                if (kind == WellKnown::Any) {
                    return anyValue(cache, *column.child, (size_t)childRow);
                }
                std::vector<int64_t> seconds(1);
                std::vector<int32_t> nanos(1);
                secondsAndNanos(*column.child, (size_t)childRow, seconds[0], nanos[0]);
                return timeValues(kind, seconds, nanos, { 1 }, Dimensions(1, 1));
            }
            if (childRow < 0) {
                return cache.absentMessage(field.messageIndex);
            }
            return messageRows(cache, *column.child, (size_t)childRow, 1);
        }
        // An absent value is [] (double), so a present empty text stays distinct.
        if (column.hasPresence && column.presence[row] == 0 && !cache.fillDefaults) {
            return cache.absent;
        }
        if (column.kind == ValueKind::Bytes) {
            return textValue(field, column.bytes[row]);
        }
        if (cache.enumsAsText && field.type == FieldType::Enum) {
            int32_t number;
            std::memcpy(&number, column.values.data() + row * 4, 4);
            return cache.enumName(field, number);
        }
        return scalarValue(column.kind, column.values.data() + row * valueSize(column.kind));
    }
    //=============================================================================
    template <typename T, typename Out>
    void
    gatherColumn(const uint8_t* source, const uint8_t* flags, const int64_t* map, size_t rows,
        Out* out, logical* present, Out missing)
    {
        for (size_t i = 0; i < rows; ++i) {
            const int64_t r = map ? map[i] : (int64_t)i;
            const bool ok = source != nullptr && r >= 0 && (flags == nullptr || flags[r] != 0);
            if (present) {
                present[i] = ok;
            }
            if (ok) {
                T value;
                std::memcpy(&value, source + (size_t)r * sizeof(T), sizeof(T));
                out[i] = (Out)value;
            } else {
                out[i] = missing;
            }
        }
    }
    //=============================================================================
    void
    gatherNative(ValueKind kind, const uint8_t* source, const uint8_t* flags, const int64_t* map,
        size_t rows, uint8_t* out, logical* present)
    {
        switch (kind) {
        case ValueKind::Double:
            gatherColumn<double, double>(source, flags, map, rows, reinterpret_cast<double*>(out),
                present, std::numeric_limits<double>::quiet_NaN());
            break;
        case ValueKind::Float:
            gatherColumn<float, float>(source, flags, map, rows, reinterpret_cast<float*>(out),
                present, std::numeric_limits<float>::quiet_NaN());
            break;
        case ValueKind::Int64:
        case ValueKind::UInt64:
            gatherColumn<uint64_t, uint64_t>(
                source, flags, map, rows, reinterpret_cast<uint64_t*>(out), present, 0);
            break;
        case ValueKind::Int32:
        case ValueKind::UInt32:
            gatherColumn<uint32_t, uint32_t>(
                source, flags, map, rows, reinterpret_cast<uint32_t*>(out), present, 0);
            break;
        case ValueKind::Bool:
            gatherColumn<uint8_t, uint8_t>(source, flags, map, rows, out, present, 0);
            break;
        default:
            break;
        }
    }
    //=============================================================================
    void
    gatherAsDouble(ValueKind kind, const uint8_t* source, const uint8_t* flags, const int64_t* map,
        size_t rows, double* out, logical* present)
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        switch (kind) {
        case ValueKind::Int64:
            gatherColumn<int64_t, double>(source, flags, map, rows, out, present, nan);
            break;
        case ValueKind::UInt64:
            gatherColumn<uint64_t, double>(source, flags, map, rows, out, present, nan);
            break;
        case ValueKind::Int32:
            gatherColumn<int32_t, double>(source, flags, map, rows, out, present, nan);
            break;
        case ValueKind::UInt32:
            gatherColumn<uint32_t, double>(source, flags, map, rows, out, present, nan);
            break;
        case ValueKind::Bool:
            gatherColumn<uint8_t, double>(source, flags, map, rows, out, present, nan);
            break;
        case ValueKind::Float:
            gatherColumn<float, double>(source, flags, map, rows, out, present, nan);
            break;
        default:
            gatherColumn<double, double>(source, flags, map, rows, out, present, nan);
            break;
        }
    }
    //=============================================================================
    ArrayOf
    messageColumns(ConversionCache& cache, const ColumnSet* set, int32_t messageIndex,
        const std::vector<int64_t>* rowMap, size_t rows, bool mayBeMissing, bool missingAsNaN,
        std::vector<int32_t>& stack, ArrayOf& presence)
    {
        const Schema& schema = cache.schema;
        const MessageDef& message = schema.messages[(size_t)messageIndex];
        auto sourceRow = [&](size_t i) -> int64_t {
            if (set == nullptr) {
                return -1;
            }
            return rowMap ? (*rowMap)[i] : (int64_t)i;
        };
        stringVector names;
        ArrayOfVector values;
        stringVector presenceNames;
        ArrayOfVector presenceValues;
        const Dimensions columnDims((indexType)rows, 1);
        const bool masked = set != nullptr && !set->keep.empty();
        for (size_t fieldIndex = 0; fieldIndex < message.fields.size(); ++fieldIndex) {
            if (masked && set->keep[fieldIndex] == 0) {
                continue;
            }
            const FieldDef& field = message.fields[fieldIndex];
            const Column* column = set ? &set->columns[fieldIndex] : nullptr;
            names.push_back(field.name);
            if (field.label == FieldLabel::Repeated) {
                ArrayOf* cells = static_cast<ArrayOf*>(ArrayOf::allocateArrayOf(
                    NLS_CELL_ARRAY, (indexType)rows, stringVector(), false));
                for (size_t i = 0; i < rows; ++i) {
                    int64_t r = sourceRow(i);
                    cells[i] = r < 0 ? cache.emptyRepeated(messageIndex, fieldIndex)
                                     : fieldValueAt(cache, *set, fieldIndex, (size_t)r);
                }
                values.push_back(ArrayOf(NLS_CELL_ARRAY, columnDims, cells));
                continue;
            }
            if (field.kind == ValueKind::Message
                && cache.wellKnown(field.messageIndex) != WellKnown::None
                && cache.wellKnown(field.messageIndex) != WellKnown::Any) {
                values.push_back(wellKnownColumn(cache, set, column, field, rowMap, rows,
                    missingAsNaN, presenceNames, presenceValues));
                continue;
            }
            if (field.kind == ValueKind::Message) {
                const ColumnSet* child = column ? column->child.get() : nullptr;
                bool recursive = false;
                for (int32_t ancestor : stack) {
                    recursive = recursive || ancestor == field.messageIndex;
                }
                if (child == nullptr && recursive) {
                    values.push_back(ArrayOf::empty(0, 0));
                    continue;
                }
                std::vector<int64_t> childMap(rows, -1);
                if (child != nullptr) {
                    for (size_t i = 0; i < rows; ++i) {
                        int64_t r = sourceRow(i);
                        childMap[i] = r < 0 ? -1 : column->childRow[(size_t)r];
                    }
                }
                ArrayOf childPresence;
                stack.push_back(field.messageIndex);
                values.push_back(messageColumns(cache, child, field.messageIndex, &childMap, rows,
                    true, missingAsNaN, stack, childPresence));
                stack.pop_back();
                // Records that have the sub-message, even with no field set.
                logical* has = static_cast<logical*>(
                    ArrayOf::allocateArrayOf(NLS_LOGICAL, (indexType)rows, stringVector(), false));
                for (size_t i = 0; i < rows; ++i) {
                    has[i] = childMap[i] >= 0;
                }
                stringVector flagNames
                    = childPresence.isStruct() ? childPresence.fieldNames() : stringVector();
                ArrayOfVector flagValues;
                for (const std::string& flagName : flagNames) {
                    flagValues.push_back(childPresence.field(flagName));
                }
                flagNames.push_back(
                    presentFlagName(cache.schema.messages[(size_t)field.messageIndex]));
                flagValues.push_back(ArrayOf(NLS_LOGICAL, Dimensions(rows, 1), has));
                presenceNames.push_back(field.name);
                presenceValues.push_back(ArrayOf::structScalar(flagNames, flagValues));
                continue;
            }
            bool canBeMissing = field.hasPresence || mayBeMissing;
            logical* present = nullptr;
            ArrayOf presentColumn;
            if (canBeMissing) {
                present = static_cast<logical*>(
                    ArrayOf::allocateArrayOf(NLS_LOGICAL, (indexType)rows, stringVector(), false));
            }
            auto isPresent = [&](int64_t r) {
                return r >= 0 && (!field.hasPresence || column->presence[(size_t)r] != 0);
            };
            if (cache.enumsAsText && field.type == FieldType::Enum) {
                ArrayOf* cells = static_cast<ArrayOf*>(ArrayOf::allocateArrayOf(
                    NLS_CELL_ARRAY, (indexType)rows, stringVector(), false));
                for (size_t i = 0; i < rows; ++i) {
                    int64_t r = sourceRow(i);
                    bool ok = isPresent(r);
                    if (present) {
                        present[i] = ok;
                    }
                    ok = ok || (cache.fillDefaults && r >= 0);
                    int32_t number = 0;
                    if (ok) {
                        std::memcpy(&number, column->values.data() + (size_t)r * 4, 4);
                    }
                    cells[i] = ok ? cache.enumName(field, number) : cache.emptyText;
                }
                values.push_back(ArrayOf(NLS_CELL_ARRAY, columnDims, cells));
            } else if (field.kind == ValueKind::Bytes) {
                ArrayOf* cells = static_cast<ArrayOf*>(ArrayOf::allocateArrayOf(
                    NLS_CELL_ARRAY, (indexType)rows, stringVector(), false));
                for (size_t i = 0; i < rows; ++i) {
                    int64_t r = sourceRow(i);
                    bool ok = isPresent(r);
                    if (present) {
                        present[i] = ok;
                    }
                    // With FillDefaults, an absent value of a present message is its default.
                    ok = ok || (cache.fillDefaults && r >= 0);
                    cells[i] = ok
                        ? textValue(field, column->bytes[(size_t)r])
                        : (field.type == FieldType::String ? cache.emptyText
                                                           : textValue(field, std::string()));
                }
                values.push_back(ArrayOf(NLS_CELL_ARRAY, columnDims, cells));
            } else {
                const size_t width = valueSize(field.kind);
                bool floating = field.kind == ValueKind::Float || field.kind == ValueKind::Double;
                bool asDouble = !floating && missingAsNaN && canBeMissing;
                const uint8_t* source = column ? column->values.data() : nullptr;
                const uint8_t* flags = column && field.hasPresence && !cache.fillDefaults
                    ? column->presence.data()
                    : nullptr;
                const int64_t* map = rowMap ? rowMap->data() : nullptr;
                if (asDouble) {
                    double* out = static_cast<double*>(ArrayOf::allocateArrayOf(
                        NLS_DOUBLE, (indexType)rows, stringVector(), false));
                    gatherAsDouble(field.kind, source, flags, map, rows, out, present);
                    values.push_back(ArrayOf(NLS_DOUBLE, columnDims, out));
                } else {
                    NelsonType type = nelsonTypeOf(field.kind);
                    uint8_t* out = static_cast<uint8_t*>(
                        ArrayOf::allocateArrayOf(type, (indexType)rows, stringVector(), false));
                    if (!canBeMissing && rowMap == nullptr && set != nullptr) {
                        // Every row is present: copy the whole column.
                        if (rows > 0) {
                            std::memcpy(out, column->values.data(), rows * width);
                        }
                    } else {
                        gatherNative(field.kind, source, flags, map, rows, out, present);
                    }
                    values.push_back(ArrayOf(type, columnDims, out));
                }
                if (present && cache.fillDefaults && field.hasPresence) {
                    // Values came from every slot: presence still comes from the flags.
                    for (size_t i = 0; i < rows; ++i) {
                        present[i] = isPresent(sourceRow(i));
                    }
                }
            }
            if (present) {
                presenceNames.push_back(field.name);
                presenceValues.push_back(ArrayOf(NLS_LOGICAL, columnDims, present));
            }
        }
        for (size_t k = 0; k < message.oneofs.size(); ++k) {
            const OneofDef& oneof = message.oneofs[k];
            if (oneof.synthetic) {
                continue;
            }
            bool kept = !masked;
            for (size_t f = 0; f < message.fields.size() && !kept; ++f) {
                kept = message.fields[f].oneofIndex == (int32_t)k && set->keep[f] != 0;
            }
            if (!kept) {
                continue;
            }
            size_t caseIndex = 0;
            if (set) {
                for (size_t j = 0; j < set->oneofIndices.size(); ++j) {
                    if (set->oneofIndices[j] == (int32_t)k) {
                        caseIndex = j;
                    }
                }
            }
            ArrayOf* cells = static_cast<ArrayOf*>(
                ArrayOf::allocateArrayOf(NLS_CELL_ARRAY, (indexType)rows, stringVector(), false));
            for (size_t i = 0; i < rows; ++i) {
                int64_t r = sourceRow(i);
                int32_t active = r < 0 ? -1 : set->oneofCase[caseIndex][(size_t)r];
                cells[i] = cache.memberName(messageIndex, active);
            }
            names.push_back(oneof.name);
            values.push_back(ArrayOf(NLS_CELL_ARRAY, columnDims, cells));
        }
        if (cache.keepUnknown && message.findFieldByName(unknownFieldName) < 0) {
            ArrayOf* cells = static_cast<ArrayOf*>(
                ArrayOf::allocateArrayOf(NLS_CELL_ARRAY, (indexType)rows, stringVector(), false));
            for (size_t i = 0; i < rows; ++i) {
                const int64_t r = sourceRow(i);
                cells[i] = r < 0 ? unknownValue(ColumnSet(), 0) : unknownValue(*set, (size_t)r);
            }
            names.push_back(unknownFieldName);
            values.push_back(ArrayOf(NLS_CELL_ARRAY, columnDims, cells));
        }
        presence = ArrayOf::structScalar(presenceNames, presenceValues);
        return ArrayOf::structScalar(names, values);
    }
    //=============================================================================
    void
    appendRecords(const Codec& codec, ColumnSet& set, const ArrayOf& value)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        stringVector names = value.fieldNames();
        const size_t nameCount = names.size();
        std::vector<int32_t> fieldSlot(message.fields.size(), -1);
        std::vector<int32_t> oneofSlot(message.oneofs.size(), -1);
        int32_t unknownSlot = -1;
        for (size_t k = 0; k < nameCount; ++k) {
            int32_t fieldIndex = message.findFieldByName(names[k]);
            if (fieldIndex >= 0) {
                fieldSlot[(size_t)fieldIndex] = (int32_t)k;
                continue;
            }
            if (names[k] == unknownFieldName) {
                unknownSlot = (int32_t)k;
                continue;
            }
            bool isOneof = false;
            for (size_t o = 0; o < message.oneofs.size(); ++o) {
                if (!message.oneofs[o].synthetic && message.oneofs[o].name == names[k]) {
                    oneofSlot[o] = (int32_t)k;
                    isOneof = true;
                }
            }
            if (!isOneof) {
                Nelson::Error("protobuf:invalidValue:unknownFieldMessage",
                    "unknown field \"%s\" for message \"%s\".", names[k], message.fullName);
            }
        }
        const indexType count = value.elementCount();
        const ArrayOf* elements = static_cast<const ArrayOf*>(value.dataPointer());
        for (indexType j = 0; j < count; ++j) {
            const ArrayOf* element = elements + j * nameCount;
            codec.beginRow(set);
            const size_t row = set.rows - 1;
            // A oneof selector names the member to write; otherwise one non-empty member at most.
            std::vector<int32_t> selected(message.oneofs.size(), -1);
            for (size_t o = 0; o < message.oneofs.size(); ++o) {
                if (oneofSlot[o] < 0) {
                    continue;
                }
                const ArrayOf& selector = element[oneofSlot[o]];
                if (!selector.isEmpty()) {
                    std::string member = selector.asUtf8String();
                    int32_t fieldIndex = message.findFieldByName(member);
                    if (fieldIndex < 0
                        || message.fields[(size_t)fieldIndex].oneofIndex != (int32_t)o) {
                        Nelson::Error("protobuf:invalidValue:memberOneof",
                            "\"%s\" is not a member of oneof \"%s\".", member,
                            message.oneofs[o].name);
                    }
                    selected[o] = fieldIndex;
                }
            }
            for (size_t fieldIndex = 0; fieldIndex < message.fields.size(); ++fieldIndex) {
                if (fieldSlot[fieldIndex] < 0) {
                    continue;
                }
                const FieldDef& field = message.fields[fieldIndex];
                const ArrayOf& fieldValue = element[fieldSlot[fieldIndex]];
                bool realOneof
                    = field.oneofIndex >= 0 && !message.oneofs[(size_t)field.oneofIndex].synthetic;
                if (realOneof) {
                    int32_t choice = selected[(size_t)field.oneofIndex];
                    bool emptyText = fieldValue.isEmpty()
                        && (fieldValue.isCharacterArray() || fieldValue.isStringArray()
                            || fieldValue.dataClass() == NLS_UINT8);
                    if ((choice >= 0 && choice != (int32_t)fieldIndex)
                        || (fieldValue.isEmpty() && !emptyText)) {
                        continue;
                    }
                    for (size_t c = 0; c < set.oneofIndices.size(); ++c) {
                        if (set.oneofIndices[c] != field.oneofIndex) {
                            continue;
                        }
                        if (set.oneofCase[c][row] >= 0) {
                            Nelson::Error("protobuf:invalidValue:severalMembersOneofSetKeep",
                                "several members of oneof \"%s\" are set; keep one or name it in "
                                "the \"%s\" field.",
                                message.oneofs[(size_t)field.oneofIndex].name,
                                message.oneofs[(size_t)field.oneofIndex].name);
                        }
                        set.oneofCase[c][row] = (int32_t)fieldIndex;
                    }
                }
                assignField(codec, set, fieldIndex, row, fieldValue);
            }
            if (unknownSlot >= 0 && !element[unknownSlot].isEmpty()) {
                const ArrayOf& bytes = element[unknownSlot];
                if (bytes.dataClass() != NLS_UINT8 || !bytes.isVector()) {
                    Nelson::Error("protobuf:invalidValue:unknownfieldsExpectsUint8Vector",
                        "UnknownFields expects a uint8 vector.");
                }
                set.unknown.resize(set.rows);
                const char* data = static_cast<const char*>(bytes.dataPointer());
                set.unknown[row].assign(data, data + bytes.elementCount());
            }
            codec.endRow(set);
        }
    }
    //=============================================================================
    void
    assignField(
        const Codec& codec, ColumnSet& set, size_t fieldIndex, size_t row, const ArrayOf& value)
    {
        const Schema& schema = codec.schema();
        const FieldDef& field = schema.messages[(size_t)set.messageIndex].fields[fieldIndex];
        Column& column = set.columns[fieldIndex];
        // Empty text or uint8 marks a present empty value (also in a StringValue or BytesValue
        // wrapper); [] marks an absent one.
        const bool text
            = value.isCharacterArray() || value.isStringArray() || value.dataClass() == NLS_UINT8;
        const bool textWrapper = column.kind == ValueKind::Message
            && wellKnownOf(schema, field.messageIndex) == WellKnown::Wrapper
            && schema.messages[(size_t)field.messageIndex].fields[0].kind == ValueKind::Bytes;
        bool presentEmpty
            = text && ((column.hasPresence && column.kind == ValueKind::Bytes) || textWrapper);
        if (value.isEmpty() && !(value.isStruct() && column.repeated) && !presentEmpty) {
            return;
        }
        const WellKnown wellKnown = column.kind == ValueKind::Message
            ? wellKnownOf(schema, field.messageIndex)
            : WellKnown::None;
        if (wellKnown == WellKnown::Any && value.isStruct() && value.isField("TypeUrl")) {
            ColumnSet& child = codec.childSet(column, field.messageIndex);
            const ArrayOf* elements = static_cast<const ArrayOf*>(value.dataPointer());
            const stringVector names = value.fieldNames();
            int64_t last = -1;
            for (indexType k = 0; k < value.elementCount(); ++k) {
                stringVector fieldNames;
                ArrayOfVector values;
                for (size_t f = 0; f < names.size(); ++f) {
                    fieldNames.push_back(names[f]);
                    values.push_back(elements[k * names.size() + f]);
                }
                last = packAny(codec, child, field, ArrayOf::structScalar(fieldNames, values));
            }
            if (!column.repeated) {
                if (value.elementCount() != 1) {
                    Nelson::Error("protobuf:invalidValue:fieldExpectsScalarStruct",
                        "field \"%s\" expects a scalar struct.", field.name);
                }
                column.childRow[row] = last;
            }
            return;
        }
        if (wellKnown == WellKnown::Any && !value.isStruct()) {
            Nelson::Error("protobuf:invalidValue:fieldExpectsStructTypeUrlMessage",
                "field \"%s\" expects a struct (TypeUrl and Message, or type_url and value).",
                field.name);
        }
        if (wellKnown != WellKnown::None && !value.isStruct()) {
            ColumnSet& child = codec.childSet(column, field.messageIndex);
            const int64_t last
                = appendWellKnown(codec, child, wellKnown, field, value, column.repeated);
            if (!column.repeated) {
                column.childRow[row] = last;
            }
            return;
        }
        if (column.repeated) {
            if (column.kind == ValueKind::Message) {
                if (!value.isStruct()) {
                    Nelson::Error("protobuf:invalidValue:fieldExpectsStructArray",
                        "field \"%s\" expects a struct array.", field.name);
                }
                appendRecords(codec, codec.childSet(column, field.messageIndex), value);
            } else if (column.kind == ValueKind::Bytes) {
                if (value.isCell()) {
                    const ArrayOf* cells = static_cast<const ArrayOf*>(value.dataPointer());
                    for (indexType k = 0; k < value.elementCount(); ++k) {
                        column.bytes.push_back(bytesFromValue(field, cells[k]));
                    }
                } else if (value.isStringArray()) {
                    for (const std::string& text : value.asUtf8StringVector(false)) {
                        column.bytes.push_back(text);
                    }
                } else {
                    column.bytes.push_back(bytesFromValue(field, value));
                }
            } else {
                std::vector<uint8_t> payload = numericPayload(schema, field, value);
                column.values.insert(column.values.end(), payload.begin(), payload.end());
            }
            return;
        }
        if (column.kind == ValueKind::Message) {
            if (!value.isStruct() || value.elementCount() != 1) {
                Nelson::Error("protobuf:invalidValue:fieldExpectsScalarStruct",
                    "field \"%s\" expects a scalar struct.", field.name);
            }
            ColumnSet& child = codec.childSet(column, field.messageIndex);
            appendRecords(codec, child, value);
            column.childRow[row] = (int64_t)child.rows - 1;
            return;
        }
        if (column.kind == ValueKind::Bytes) {
            column.bytes[row] = bytesFromValue(field, value);
        } else {
            std::vector<uint8_t> payload = numericPayload(schema, field, value);
            if (payload.size() != valueSize(column.kind)) {
                Nelson::Error("protobuf:invalidValue:fieldExpectsScalarValue",
                    "field \"%s\" expects a scalar value.", field.name);
            }
            std::memcpy(
                column.values.data() + row * payload.size(), payload.data(), payload.size());
        }
        if (column.hasPresence) {
            column.presence[row] = 1;
        }
    }
    //=============================================================================
    std::string
    bytesFromValue(const FieldDef& field, const ArrayOf& value)
    {
        if (value.isEmpty()) {
            return std::string();
        }
        if (value.isRowVectorCharacterArray() || (value.isStringArray() && value.isScalar())) {
            return value.asUtf8String();
        }
        if (field.type == FieldType::Bytes && value.dataClass() == NLS_UINT8 && value.isVector()) {
            const char* data = static_cast<const char*>(value.dataPointer());
            return std::string(data, data + value.elementCount());
        }
        if (field.type == FieldType::String) {
            Nelson::Error(
                "protobuf:invalidValue:fieldExpectsText", "field \"%s\" expects text.", field.name);
        }
        Nelson::Error("protobuf:invalidValue:fieldExpectsBytes",
            "field \"%s\" expects a uint8 vector or text.", field.name);
    }
    //=============================================================================
    std::vector<uint8_t>
    numericPayload(const Schema& schema, const FieldDef& field, const ArrayOf& value)
    {
        ArrayOf numbers = value;
        if (field.type == FieldType::Enum
            && (value.isRowVectorCharacterArray() || value.isCell() || value.isStringArray())) {
            // Enum values may be given by name.
            const EnumDef& enumDef = schema.enums[(size_t)field.enumIndex];
            stringVector valueNames = value.isRowVectorCharacterArray()
                ? stringVector { value.asUtf8String() }
                : value.asUtf8StringVector(false);
            std::vector<int32_t> resolved;
            for (const std::string& name : valueNames) {
                bool found = false;
                for (const EnumValueDef& v : enumDef.values) {
                    if (v.name == name) {
                        resolved.push_back(v.number);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    // A number without a name, as EnumsAsText gives it.
                    char* end = nullptr;
                    const long long number = std::strtoll(name.c_str(), &end, 10);
                    if (name.empty() || *end != '\0' || number < INT32_MIN || number > INT32_MAX) {
                        Nelson::Error("protobuf:invalidValue:notAnEnumValue",
                            "\"%s\" is not a value of enum \"%s\".", name, enumDef.fullName);
                    }
                    resolved.push_back((int32_t)number);
                }
            }
            std::vector<uint8_t> payload(resolved.size() * 4);
            if (!resolved.empty()) {
                std::memcpy(payload.data(), resolved.data(), payload.size());
            }
            return payload;
        }
        if (!(value.isNumeric() || value.isLogical()) || value.isSparse() || value.isComplex()) {
            Nelson::Error("protobuf:invalidValue:fieldExpectsRealNumericValues",
                "field \"%s\" expects real numeric values.", field.name);
        }
        numbers.promoteType(nelsonTypeOf(field.kind));
        const size_t bytes = (size_t)numbers.elementCount() * valueSize(field.kind);
        std::vector<uint8_t> payload(bytes);
        if (bytes > 0) {
            std::memcpy(payload.data(), numbers.dataPointer(), bytes);
        }
        return payload;
    }
    //=============================================================================
    ConversionCache::ConversionCache(const Codec& codecRef, bool convertWellKnown, bool unknown)
        : codec(codecRef)
        , schema(codecRef.schema())
        , wellKnownTypes(convertWellKnown)
        , keepUnknown(unknown)
        , absent(ArrayOf::empty(0, 0))
        , emptyText(ArrayOf::characterArray(std::string()))
    {
        messages.resize(schema.messages.size());
    }
    //=============================================================================
    ConversionCache::PerMessage&
    ConversionCache::message(int32_t messageIndex)
    {
        PerMessage& entry = messages[(size_t)messageIndex];
        if (!entry.ready) {
            const MessageDef& def = schema.messages[(size_t)messageIndex];
            entry.names = structFieldNames(def, keepUnknown);
            Dimensions zero(0, 0);
            entry.absentMessage = ArrayOf::emptyStruct(entry.names, zero);
            for (const FieldDef& field : def.fields) {
                if (enumsAsText && field.label == FieldLabel::Repeated
                    && field.type == FieldType::Enum) {
                    entry.emptyRepeated.push_back(ArrayOf::emptyCell(Dimensions(1, 0)));
                } else {
                    entry.emptyRepeated.push_back(field.label == FieldLabel::Repeated
                            ? emptyRepeatedValue(schema, field, keepUnknown)
                            : ArrayOf());
                }
                entry.memberNames.push_back(ArrayOf::characterArray(field.name));
            }
            entry.wellKnown = wellKnownOf(schema, messageIndex);
            entry.ready = true;
        }
        return entry;
    }
    //=============================================================================
    const stringVector&
    ConversionCache::names(int32_t messageIndex)
    {
        return message(messageIndex).names;
    }
    //=============================================================================
    const ArrayOf&
    ConversionCache::absentMessage(int32_t messageIndex)
    {
        return message(messageIndex).absentMessage;
    }
    //=============================================================================
    const ArrayOf&
    ConversionCache::emptyRepeated(int32_t messageIndex, size_t fieldIndex)
    {
        return message(messageIndex).emptyRepeated[fieldIndex];
    }
    //=============================================================================
    const ArrayOf&
    ConversionCache::memberName(int32_t messageIndex, int32_t fieldIndex)
    {
        if (fieldIndex < 0) {
            return emptyText;
        }
        return message(messageIndex).memberNames[(size_t)fieldIndex];
    }
    //=============================================================================
    WellKnown
    wellKnownOf(const Schema& schema, int32_t messageIndex)
    {
        const MessageDef& message = schema.messages[(size_t)messageIndex];
        const std::string prefix = "google.protobuf.";
        if (message.fullName.compare(0, prefix.size(), prefix) != 0) {
            return WellKnown::None;
        }
        const std::string name = message.fullName.substr(prefix.size());
        if ((name == "Timestamp" || name == "Duration") && message.fields.size() == 2
            && message.fields[0].name == "seconds" && message.fields[1].name == "nanos") {
            return name == "Timestamp" ? WellKnown::Timestamp : WellKnown::Duration;
        }
        static const char* wrappers[] = { "DoubleValue", "FloatValue", "Int64Value", "UInt64Value",
            "Int32Value", "UInt32Value", "BoolValue", "StringValue", "BytesValue" };
        for (const char* wrapper : wrappers) {
            if (name == wrapper && message.fields.size() == 1
                && message.fields[0].name == "value") {
                return WellKnown::Wrapper;
            }
        }
        if (name == "Any" && message.fields.size() == 2 && message.fields[0].name == "type_url"
            && message.fields[1].name == "value") {
            return WellKnown::Any;
        }
        return WellKnown::None;
    }
    //=============================================================================
    ArrayOf
    anyValue(ConversionCache& cache, const ColumnSet& set, size_t row)
    {
        const std::string& url = set.columns[0].bytes[row];
        const std::string& packed = set.columns[1].bytes[row];
        ArrayOf message = ArrayOf::empty(0, 0);
        const size_t slash = url.find_last_of('/');
        const int32_t type
            = cache.schema.findMessage(slash == std::string::npos ? url : url.substr(slash + 1));
        if (type >= 0) {
            ColumnSet inner = cache.codec.createColumnSet(type);
            try {
                cache.codec.appendMessage(
                    inner, reinterpret_cast<const uint8_t*>(packed.data()), packed.size());
                const WellKnown kind = cache.wellKnown(type);
                if (kind == WellKnown::Timestamp || kind == WellKnown::Duration) {
                    std::vector<int64_t> seconds(1);
                    std::vector<int32_t> nanos(1);
                    secondsAndNanos(inner, 0, seconds[0], nanos[0]);
                    message = timeValues(kind, seconds, nanos, { 1 }, Dimensions(1, 1));
                } else if (kind == WellKnown::Wrapper) {
                    message = fieldValueAt(cache, inner, 0, 0);
                } else {
                    message = messageRows(cache, inner, 0, 1);
                }
            } catch (const ProtoError&) {
                message = ArrayOf::empty(0, 0);
            }
        }
        void* data
            = ArrayOf::allocateArrayOf(NLS_UINT8, (indexType)packed.size(), stringVector(), false);
        if (!packed.empty()) {
            std::memcpy(data, packed.data(), packed.size());
        }
        ArrayOfVector values;
        values.push_back(ArrayOf::characterArray(url));
        values.push_back(
            ArrayOf(NLS_UINT8, Dimensions(packed.empty() ? 0 : 1, (indexType)packed.size()), data));
        values.push_back(message);
        const stringVector names = { "TypeUrl", "Value", "Message" };
        return ArrayOf::structScalar(names, values);
    }
    //=============================================================================
    int64_t
    packAny(const Codec& codec, ColumnSet& child, const FieldDef& field, const ArrayOf& value)
    {
        const Schema& schema = codec.schema();
        const std::string url = value.field("TypeUrl").asUtf8String();
        std::string packed;
        const bool withMessage = value.isField("Message") && !value.field("Message").isEmpty();
        if (withMessage) {
            const size_t slash = url.find_last_of('/');
            const int32_t type
                = schema.findMessage(slash == std::string::npos ? url : url.substr(slash + 1));
            if (type < 0) {
                Nelson::Error("protobuf:invalidValue:fieldTypeSchema",
                    "field \"%s\": type of \"%s\" is not in the schema.", field.name, url);
            }
            ColumnSet inner = codec.createColumnSet(type);
            const ArrayOf message = value.field("Message");
            const WellKnown kind = wellKnownOf(schema, type);
            if (kind != WellKnown::None && kind != WellKnown::Any && !message.isStruct()) {
                // A packed well-known type given as datetime, duration or value.
                FieldDef holder = field;
                holder.name = "Message";
                appendWellKnown(codec, inner, kind, holder, message, false);
            } else {
                appendStructRows(codec, inner, message);
            }
            if (inner.rows != 1) {
                Nelson::Error("protobuf:invalidValue:fieldMessageMessage",
                    "field \"%s\": Message must be one message.", field.name);
            }
            packed = codec.encodeRow(inner, 0);
        } else if (value.isField("Value") && !value.field("Value").isEmpty()) {
            const ArrayOf bytes = value.field("Value");
            if (bytes.dataClass() != NLS_UINT8) {
                Nelson::Error("protobuf:invalidValue:fieldValueExpectsUint8Vector",
                    "field \"%s\": Value expects a uint8 vector.", field.name);
            }
            const char* data = static_cast<const char*>(bytes.dataPointer());
            packed.assign(data, data + bytes.elementCount());
        }
        codec.beginRow(child);
        const size_t row = child.rows - 1;
        child.columns[0].bytes[row] = url;
        child.columns[1].bytes[row] = packed;
        codec.endRow(child);
        return (int64_t)row;
    }
    //=============================================================================
    void
    secondsAndNanos(const ColumnSet& set, size_t row, int64_t& seconds, int32_t& nanos)
    {
        std::memcpy(&seconds, set.columns[0].values.data() + row * 8, 8);
        std::memcpy(&nanos, set.columns[1].values.data() + row * 4, 4);
    }
    //=============================================================================
    // datetime (UTC) or duration values; the time of day is kept in milliseconds so that
    // nanoseconds below the millisecond survive as fractions.
    ArrayOf
    timeValues(WellKnown kind, const std::vector<int64_t>& seconds,
        const std::vector<int32_t>& nanos, const std::vector<uint8_t>& present,
        const Dimensions& dims)
    {
        const size_t n = seconds.size();
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (kind == WellKnown::Duration) {
            double* values = static_cast<double*>(
                ArrayOf::allocateArrayOf(NLS_DOUBLE, (indexType)n, stringVector(), false));
            for (size_t k = 0; k < n; ++k) {
                values[k] = present[k] ? (double)seconds[k] + nanos[k] * 1e-9 : nan;
            }
            ArrayOfVector fields;
            fields.push_back(ArrayOf(NLS_DOUBLE, dims, values));
            fields.push_back(ArrayOf::characterArray("hh:mm:ss"));
            return ArrayOf::classdefArray("duration", { "SecondsValue", "FormatValue" }, fields);
        }
        double* serial = static_cast<double*>(
            ArrayOf::allocateArrayOf(NLS_DOUBLE, (indexType)n, stringVector(), false));
        double* millis = static_cast<double*>(
            ArrayOf::allocateArrayOf(NLS_DOUBLE, (indexType)n, stringVector(), false));
        for (size_t k = 0; k < n; ++k) {
            if (!present[k]) {
                serial[k] = nan;
                millis[k] = nan;
                continue;
            }
            int64_t day = seconds[k] / 86400;
            if (seconds[k] % 86400 < 0) {
                day--;
            }
            const double ofDay = (double)(seconds[k] - day * 86400) * 1000.0 + nanos[k] * 1e-6;
            millis[k] = ofDay;
            serial[k] = 719529.0 + (double)day + ofDay / 86400000.0;
        }
        ArrayOfVector fields;
        fields.push_back(ArrayOf(NLS_DOUBLE, dims, serial));
        fields.push_back(ArrayOf::characterArray("default"));
        fields.push_back(ArrayOf::characterArray("UTC"));
        fields.push_back(ArrayOf(NLS_DOUBLE, dims, millis));
        fields.push_back(ArrayOf::empty(0, 0));
        return ArrayOf::classdefArray("datetime",
            { "SerialValue", "FormatValue", "TimeZoneValue", "TimeMillis", "FoldValue" }, fields);
    }
    //=============================================================================
    ArrayOf
    wellKnownColumn(ConversionCache& cache, const ColumnSet* set, const Column* column,
        const FieldDef& field, const std::vector<int64_t>* rowMap, size_t rows, bool missingAsNaN,
        stringVector& presenceNames, ArrayOfVector& presenceValues)
    {
        const ColumnSet* child = column ? column->child.get() : nullptr;
        std::vector<int64_t> childRows(rows, -1);
        if (child != nullptr) {
            for (size_t i = 0; i < rows; ++i) {
                const int64_t r = rowMap ? (*rowMap)[i] : (int64_t)i;
                childRows[i] = r < 0 ? -1 : column->childRow[(size_t)r];
            }
        }
        const Dimensions dims((indexType)rows, 1);
        logical* present = static_cast<logical*>(
            ArrayOf::allocateArrayOf(NLS_LOGICAL, (indexType)rows, stringVector(), false));
        for (size_t i = 0; i < rows; ++i) {
            present[i] = childRows[i] >= 0;
        }
        presenceNames.push_back(field.name);
        presenceValues.push_back(ArrayOf(NLS_LOGICAL, dims, present));
        const WellKnown kind = cache.wellKnown(field.messageIndex);
        if (kind == WellKnown::Timestamp || kind == WellKnown::Duration) {
            std::vector<int64_t> seconds(rows, 0);
            std::vector<int32_t> nanos(rows, 0);
            std::vector<uint8_t> flags(rows, 0);
            for (size_t i = 0; i < rows; ++i) {
                if (childRows[i] >= 0) {
                    secondsAndNanos(*child, (size_t)childRows[i], seconds[i], nanos[i]);
                    flags[i] = 1;
                }
            }
            return timeValues(kind, seconds, nanos, flags, dims);
        }
        // Wrapper: the column of its value, missing where the wrapper is absent.
        const FieldDef& valueField = cache.schema.messages[(size_t)field.messageIndex].fields[0];
        const Column* inner = child ? &child->columns[0] : nullptr;
        if (valueField.kind == ValueKind::Bytes) {
            ArrayOf* cells = static_cast<ArrayOf*>(
                ArrayOf::allocateArrayOf(NLS_CELL_ARRAY, (indexType)rows, stringVector(), false));
            for (size_t i = 0; i < rows; ++i) {
                cells[i] = childRows[i] >= 0
                    ? textValue(valueField, inner->bytes[(size_t)childRows[i]])
                    : cache.emptyText;
            }
            return ArrayOf(NLS_CELL_ARRAY, dims, cells);
        }
        const uint8_t* source = inner ? inner->values.data() : nullptr;
        const bool floating
            = valueField.kind == ValueKind::Float || valueField.kind == ValueKind::Double;
        if (missingAsNaN && !floating) {
            double* out = static_cast<double*>(
                ArrayOf::allocateArrayOf(NLS_DOUBLE, (indexType)rows, stringVector(), false));
            gatherAsDouble(valueField.kind, source, nullptr, childRows.data(), rows, out, nullptr);
            return ArrayOf(NLS_DOUBLE, dims, out);
        }
        NelsonType type = nelsonTypeOf(valueField.kind);
        uint8_t* out = static_cast<uint8_t*>(
            ArrayOf::allocateArrayOf(type, (indexType)rows, stringVector(), false));
        gatherNative(valueField.kind, source, nullptr, childRows.data(), rows, out, nullptr);
        return ArrayOf(type, dims, out);
    }
    //=============================================================================
    int64_t
    appendWellKnown(const Codec& codec, ColumnSet& child, WellKnown kind, const FieldDef& field,
        const ArrayOf& value, bool repeated)
    {
        const Schema& schema = codec.schema();
        const FieldDef& inner = schema.messages[(size_t)child.messageIndex].fields[0];
        std::vector<int64_t> seconds;
        std::vector<int32_t> nanos;
        if (kind == WellKnown::Timestamp || kind == WellKnown::Duration) {
            std::vector<uint8_t> valid;
            timeParts(kind, field, value, seconds, nanos, valid);
            if (!repeated && seconds.size() != 1) {
                Nelson::Error("protobuf:invalidValue:fieldExpectsScalarValue",
                    "field \"%s\" expects a scalar value.", field.name);
            }
            if (repeated && std::find(valid.begin(), valid.end(), 0) != valid.end()) {
                Nelson::Error("protobuf:invalidValue:fieldHoldMissingValuesNaT",
                    "field \"%s\" cannot hold missing values (NaT or NaN).", field.name);
            }
            if (!valid.empty() && valid[0] == 0) {
                return -1;
            }
            int64_t last = -1;
            for (size_t k = 0; k < seconds.size(); ++k) {
                codec.beginRow(child);
                const size_t row = child.rows - 1;
                std::memcpy(child.columns[0].values.data() + row * 8, &seconds[k], 8);
                std::memcpy(child.columns[1].values.data() + row * 4, &nanos[k], 4);
                codec.endRow(child);
                last = (int64_t)row;
            }
            return last;
        }
        // Wrappers: one row per value.
        int64_t last = -1;
        if (inner.kind == ValueKind::Bytes) {
            stringVector texts;
            if (value.isCell()) {
                const ArrayOf* cells = static_cast<const ArrayOf*>(value.dataPointer());
                for (indexType k = 0; k < value.elementCount(); ++k) {
                    texts.push_back(bytesFromValue(inner, cells[k]));
                }
            } else if (value.isStringArray() && !value.isScalar()) {
                texts = value.asUtf8StringVector(false);
            } else {
                texts.push_back(bytesFromValue(inner, value));
            }
            if (!repeated && texts.size() != 1) {
                Nelson::Error("protobuf:invalidValue:fieldExpectsScalarValue",
                    "field \"%s\" expects a scalar value.", field.name);
            }
            for (const std::string& text : texts) {
                codec.beginRow(child);
                child.columns[0].bytes[child.rows - 1] = text;
                codec.endRow(child);
                last = (int64_t)child.rows - 1;
            }
            return last;
        }
        std::vector<uint8_t> payload;
        if (value.isCell()) {
            // Repeated wrappers may also be given as a cell of values.
            const ArrayOf* cells = static_cast<const ArrayOf*>(value.dataPointer());
            for (indexType k = 0; k < value.elementCount(); ++k) {
                std::vector<uint8_t> one = numericPayload(schema, inner, cells[k]);
                payload.insert(payload.end(), one.begin(), one.end());
            }
        } else if (!(value.isNumeric() || value.isLogical()) && inner.type != FieldType::Enum) {
            Nelson::Error("protobuf:invalidValue:fieldExpectsRealNumericValues",
                "field \"%s\" expects real numeric values.", field.name);
        } else {
            payload = numericPayload(schema, inner, value);
        }
        const size_t width = valueSize(inner.kind);
        const size_t n = payload.size() / width;
        if (!repeated && n != 1) {
            Nelson::Error("protobuf:invalidValue:fieldExpectsScalarValue",
                "field \"%s\" expects a scalar value.", field.name);
        }
        for (size_t k = 0; k < n; ++k) {
            codec.beginRow(child);
            std::memcpy(child.columns[0].values.data() + (child.rows - 1) * width,
                payload.data() + k * width, width);
            codec.endRow(child);
            last = (int64_t)child.rows - 1;
        }
        return last;
    }
    //=============================================================================
    void
    timeParts(WellKnown kind, const FieldDef& field, const ArrayOf& value,
        std::vector<int64_t>& seconds, std::vector<int32_t>& nanos, std::vector<uint8_t>& valid)
    {
        const std::string className = kind == WellKnown::Timestamp ? "datetime" : "duration";
        if (!value.isClassType() || value.classType() != className) {
            Nelson::Error("protobuf:invalidValue:fieldExpectsValueStruct",
                "field \"%s\" expects a %s value or a struct.", field.name, className);
        }
        ArrayOf numbers
            = value.field(kind == WellKnown::Timestamp ? "SerialValue" : "SecondsValue");
        numbers.promoteType(NLS_DOUBLE);
        const double* data = static_cast<const double*>(numbers.dataPointer());
        const size_t n = (size_t)numbers.elementCount();
        std::vector<double> millis;
        if (kind == WellKnown::Timestamp) {
            const std::string zone = value.field("TimeZoneValue").asUtf8String();
            if (!zone.empty() && zone != "UTC" && zone != "Z" && zone != "+00:00") {
                Nelson::Error("protobuf:invalidValue:fieldExpectsUTCDatetimeValues",
                    "field \"%s\" expects UTC datetime values (or no time zone).", field.name);
            }
            ArrayOf ofDay = value.field("TimeMillis");
            if (!ofDay.isEmpty()) {
                ofDay.promoteType(NLS_DOUBLE);
                const double* m = static_cast<const double*>(ofDay.dataPointer());
                millis.assign(m, m + ofDay.elementCount());
            }
        }
        seconds.assign(n, 0);
        nanos.assign(n, 0);
        valid.assign(n, 0);
        for (size_t k = 0; k < n; ++k) {
            if (std::isnan(data[k])) {
                continue;
            }
            double whole = 0;
            double fraction = 0;
            if (kind == WellKnown::Timestamp) {
                const double ofDay
                    = millis.size() == n ? millis[k] : (data[k] - std::floor(data[k])) * 86400000.0;
                const double day = std::round(data[k] - ofDay / 86400000.0);
                const double secondsOfDay = std::floor(ofDay / 1000.0);
                whole = (day - 719529.0) * 86400.0 + secondsOfDay;
                fraction = (ofDay - secondsOfDay * 1000.0) * 1e6;
            } else {
                whole = std::floor(data[k]);
                fraction = (data[k] - whole) * 1e9;
                // Durations keep the sign of seconds on nanos.
                if (whole < 0 && fraction > 0) {
                    whole += 1;
                    fraction -= 1e9;
                }
            }
            // Seconds beyond the int64 range (doubles lose these digits anyway) saturate.
            const double limit = 9223372036854775807.0;
            int64_t sec
                = whole >= limit ? INT64_MAX : (whole <= -limit ? INT64_MIN : (int64_t)whole);
            int64_t ns = (int64_t)std::llround(fraction);
            if (ns >= 1000000000) {
                sec++;
                ns -= 1000000000;
            } else if (ns <= -1000000000) {
                sec--;
                ns += 1000000000;
            }
            seconds[k] = sec;
            nanos[k] = (int32_t)ns;
            valid[k] = 1;
        }
    }
    //=============================================================================
    // Number of rows of a column (struct of columns: of its first column).
    size_t
    columnRows(const ArrayOf& column)
    {
        if (column.isStruct() && column.isScalar()) {
            for (const std::string& name : column.fieldNames()) {
                const size_t n = columnRows(column.field(name));
                if (n > 0) {
                    return n;
                }
            }
            return 0;
        }
        if (column.isClassType()) {
            const std::string name = column.classType();
            if (name == "datetime" || name == "duration") {
                return (size_t)column.field(name == "datetime" ? "SerialValue" : "SecondsValue")
                    .elementCount();
            }
        }
        return (size_t)column.elementCount();
    }
    //=============================================================================
    // Text of each row of a cell or string column.
    stringVector
    textCells(
        const FieldDef& field, const ArrayOf& column, size_t rows, std::vector<uint8_t>& empty)
    {
        stringVector texts(rows);
        empty.assign(rows, 1);
        if (column.isStringArray()) {
            stringVector all = column.asUtf8StringVector(false);
            if (all.size() != rows) {
                Nelson::Error("protobuf:invalidValue:columnRows",
                    "column \"%s\" must have %d rows.", field.name, rows);
            }
            for (size_t i = 0; i < rows; ++i) {
                texts[i] = all[i];
                empty[i] = all[i].empty();
            }
            return texts;
        }
        if (!column.isCell() || (size_t)column.elementCount() != rows) {
            Nelson::Error("protobuf:invalidValue:columnExpectsNx1CellString",
                "column \"%s\" expects an Nx1 cell or string array.", field.name);
        }
        const ArrayOf* cells = static_cast<const ArrayOf*>(column.dataPointer());
        for (size_t i = 0; i < rows; ++i) {
            empty[i] = cells[i].isEmpty();
            if (!empty[i]) {
                texts[i] = bytesFromValue(field, cells[i]);
            }
        }
        return texts;
    }
    //=============================================================================
    // Rows where a numeric column holds no value: presence flags, else NaN in a column that can
    // hold missing values (fields with presence, or integers given as double).
    std::vector<uint8_t>
    missingRows(const FieldDef& valueField, bool hasPresence, const ArrayOf& column,
        const ArrayOf* flags, size_t rows)
    {
        std::vector<uint8_t> missing(rows, 0);
        if (flags != nullptr) {
            const logical* f = static_cast<const logical*>(flags->dataPointer());
            for (size_t i = 0; i < rows; ++i) {
                missing[i] = f[i] ? 0 : 1;
            }
            return missing;
        }
        const bool floatingField
            = valueField.kind == ValueKind::Float || valueField.kind == ValueKind::Double;
        if ((column.dataClass() == NLS_DOUBLE || column.dataClass() == NLS_SINGLE)
            && (hasPresence || !floatingField)) {
            const size_t n = std::min(rows, (size_t)column.elementCount());
            if (column.dataClass() == NLS_DOUBLE) {
                const double* d = static_cast<const double*>(column.dataPointer());
                for (size_t i = 0; i < n; ++i) {
                    missing[i] = std::isnan(d[i]) ? 1 : 0;
                }
            } else {
                const float* f = static_cast<const float*>(column.dataPointer());
                for (size_t i = 0; i < n; ++i) {
                    missing[i] = std::isnan(f[i]) ? 1 : 0;
                }
            }
        }
        return missing;
    }
    //=============================================================================
    // Bytes of each row of a numeric column, in the natural type of the field (NaN rows as 0).
    std::vector<uint8_t>
    columnPayload(const Schema& schema, const FieldDef& field, const ArrayOf& column, size_t rows)
    {
        ArrayOf numbers = column;
        const bool floatingField
            = field.kind == ValueKind::Float || field.kind == ValueKind::Double;
        if (!floatingField
            && (column.dataClass() == NLS_DOUBLE || column.dataClass() == NLS_SINGLE)) {
            numbers.promoteType(NLS_DOUBLE);
            double* data = static_cast<double*>(
                ArrayOf::allocateArrayOf(NLS_DOUBLE, (indexType)rows, stringVector(), false));
            std::memcpy(data, numbers.dataPointer(), rows * sizeof(double));
            for (size_t i = 0; i < rows; ++i) {
                if (std::isnan(data[i])) {
                    data[i] = 0;
                }
            }
            numbers = ArrayOf(NLS_DOUBLE, Dimensions((indexType)rows, 1), data);
        }
        std::vector<uint8_t> payload = numericPayload(schema, field, numbers);
        if (payload.size() != rows * valueSize(field.kind)) {
            Nelson::Error("protobuf:invalidValue:columnRows", "column \"%s\" must have %d rows.",
                field.name, rows);
        }
        return payload;
    }
    //=============================================================================
    std::vector<uint8_t>
    appendColumnRows(const Codec& codec, ColumnSet& set, const ArrayOf& columns,
        const ArrayOf& presence, size_t parentRows)
    {
        const Schema& schema = codec.schema();
        const MessageDef& message = schema.messages[(size_t)set.messageIndex];
        size_t rows = columnRows(columns);
        if (parentRows != SIZE_MAX) {
            if (columns.fieldNames().empty()) {
                rows = parentRows;
            } else if (rows != parentRows) {
                Nelson::Error("protobuf:invalidValue:columnsSubMessageRows",
                    "the columns of sub-message \"%s\" must have %d rows.", message.fullName,
                    parentRows);
            }
        }
        const size_t base = set.rows;
        std::vector<uint8_t> has(rows, 0);
        codec.reserveRows(set, base + rows);
        for (size_t i = 0; i < rows; ++i) {
            codec.beginRow(set);
        }
        const stringVector names = columns.fieldNames();
        const bool withPresence = presence.isStruct() && presence.isScalar();
        // Oneof columns name the member written in each row.
        std::vector<stringVector> selection(message.oneofs.size());
        for (size_t o = 0; o < message.oneofs.size(); ++o) {
            if (!message.oneofs[o].synthetic && columns.isField(message.oneofs[o].name)) {
                std::vector<uint8_t> empty;
                FieldDef selector;
                selector.name = message.oneofs[o].name;
                selector.type = FieldType::String;
                selection[o]
                    = textCells(selector, columns.field(message.oneofs[o].name), rows, empty);
            }
        }
        for (const std::string& name : names) {
            const ArrayOf column = columns.field(name);
            if (!column.isStruct() && !(column.isNumeric() && column.isEmpty())
                && columnRows(column) != rows) {
                Nelson::Error("protobuf:invalidValue:columnRows",
                    "column \"%s\" must have %d rows.", name, rows);
            }
            const int32_t fieldIndex = message.findFieldByName(name);
            if (fieldIndex < 0) {
                bool isOneof = false;
                for (const OneofDef& oneof : message.oneofs) {
                    isOneof = isOneof || (!oneof.synthetic && oneof.name == name);
                }
                if (isOneof) {
                    continue;
                }
                if (name != unknownFieldName) {
                    Nelson::Error("protobuf:invalidValue:unknownFieldMessage",
                        "unknown field \"%s\" for message \"%s\".", name, message.fullName);
                }
                if (!column.isCell() || (size_t)column.elementCount() != rows) {
                    Nelson::Error("protobuf:invalidValue:columnExpectsNx1CellUint8",
                        "column \"%s\" expects an Nx1 cell of uint8 vectors.", name);
                }
                const ArrayOf* cells = static_cast<const ArrayOf*>(column.dataPointer());
                set.unknown.resize(set.rows);
                for (size_t i = 0; i < rows; ++i) {
                    if (!cells[i].isEmpty()) {
                        if (cells[i].dataClass() != NLS_UINT8) {
                            Nelson::Error("protobuf:invalidValue:unknownfieldsExpectsUint8Vectors",
                                "UnknownFields expects uint8 vectors.");
                        }
                        const char* data = static_cast<const char*>(cells[i].dataPointer());
                        set.unknown[base + i].assign(data, data + cells[i].elementCount());
                        has[i] = 1;
                    }
                }
                continue;
            }
            const FieldDef& field = message.fields[(size_t)fieldIndex];
            Column& target = set.columns[(size_t)fieldIndex];
            const ArrayOf* flags = nullptr;
            ArrayOf flagColumn;
            ArrayOf childPresence;
            if (withPresence && presence.isField(name)) {
                flagColumn = presence.field(name);
                if (flagColumn.isLogical() && (size_t)flagColumn.elementCount() == rows) {
                    flags = &flagColumn;
                } else if (flagColumn.isStruct()) {
                    childPresence = flagColumn;
                }
            }
            // Rows where a oneof column names another member hold no value for this field.
            std::vector<uint8_t> selected(rows, 1);
            bool chosen = false;
            if (field.oneofIndex >= 0 && !selection[(size_t)field.oneofIndex].empty()) {
                chosen = true;
                for (size_t i = 0; i < rows; ++i) {
                    selected[i] = selection[(size_t)field.oneofIndex][i] == field.name;
                }
            }
            auto markOneof = [&](size_t i) {
                if (field.oneofIndex >= 0 && !message.oneofs[(size_t)field.oneofIndex].synthetic) {
                    for (size_t c = 0; c < set.oneofIndices.size(); ++c) {
                        if (set.oneofIndices[c] == field.oneofIndex) {
                            set.oneofCase[c][base + i] = fieldIndex;
                        }
                    }
                }
            };
            if (field.label == FieldLabel::Repeated) {
                if (!column.isCell() || (size_t)column.elementCount() != rows) {
                    Nelson::Error("protobuf:invalidValue:columnExpectsNx1CellValue",
                        "column \"%s\" expects an Nx1 cell (one value per record).", name);
                }
                const ArrayOf* cells = static_cast<const ArrayOf*>(column.dataPointer());
                // The offsets of this column are written here, row by row.
                target.offsets.resize(base + 1);
                for (size_t i = 0; i < rows; ++i) {
                    assignField(codec, set, (size_t)fieldIndex, base + i, cells[i]);
                    target.offsets.push_back(target.count());
                    has[i] = has[i] || target.offsets[base + i + 1] > target.offsets[base + i];
                }
                continue;
            }
            if (field.kind == ValueKind::Message) {
                const WellKnown kind = wellKnownOf(schema, field.messageIndex);
                ColumnSet& child = codec.childSet(target, field.messageIndex);
                if (kind == WellKnown::None || kind == WellKnown::Any || column.isStruct()) {
                    if (column.isNumeric() && column.isEmpty()) {
                        continue;
                    }
                    if (!column.isStruct() || !column.isScalar()) {
                        Nelson::Error("protobuf:invalidValue:columnExpectsStructColumns",
                            "column \"%s\" expects a struct of columns.", name);
                    }
                    const size_t childBase = child.rows;
                    std::vector<uint8_t> childHas
                        = appendColumnRows(codec, child, column, childPresence, rows);
                    // The Present flag of the presence, when given, says which records have it.
                    const std::string flagName
                        = presentFlagName(schema.messages[(size_t)field.messageIndex]);
                    ArrayOf flag;
                    const logical* given = nullptr;
                    if (childPresence.isStruct() && childPresence.isField(flagName)) {
                        flag = childPresence.field(flagName);
                        if (flag.isLogical() && (size_t)flag.elementCount() == rows) {
                            given = static_cast<const logical*>(flag.dataPointer());
                        }
                    }
                    for (size_t i = 0; i < rows; ++i) {
                        bool present = given ? given[i] != 0 : childHas[i] != 0;
                        present
                            = chosen ? selected[i] != 0 && (given == nullptr || present) : present;
                        if (present) {
                            target.childRow[base + i] = (int64_t)(childBase + i);
                            markOneof(i);
                            has[i] = 1;
                        }
                    }
                    continue;
                }
                // Well-known type column: datetime, duration, or the values of a wrapper.
                const FieldDef& inner = schema.messages[(size_t)field.messageIndex].fields[0];
                std::vector<uint8_t> present(rows, 0);
                std::vector<int64_t> seconds;
                std::vector<int32_t> nanos;
                stringVector texts;
                std::vector<uint8_t> payload;
                if (kind == WellKnown::Timestamp || kind == WellKnown::Duration) {
                    timeParts(kind, field, column, seconds, nanos, present);
                    if (present.size() != rows) {
                        Nelson::Error("protobuf:invalidValue:columnRows",
                            "column \"%s\" must have %d rows.", name, rows);
                    }
                } else if (inner.kind == ValueKind::Bytes) {
                    std::vector<uint8_t> empty;
                    texts = textCells(inner, column, rows, empty);
                    for (size_t i = 0; i < rows; ++i) {
                        present[i] = empty[i] ? 0 : 1;
                    }
                } else {
                    payload = columnPayload(schema, inner, column, rows);
                    std::vector<uint8_t> missing = missingRows(inner, true, column, nullptr, rows);
                    for (size_t i = 0; i < rows; ++i) {
                        present[i] = missing[i] ? 0 : 1;
                    }
                }
                const size_t width = valueSize(inner.kind);
                for (size_t i = 0; i < rows; ++i) {
                    bool keep = flags ? static_cast<const logical*>(flags->dataPointer())[i] != 0
                                      : present[i] != 0;
                    if (chosen) {
                        keep = selected[i] != 0;
                    }
                    if (!keep) {
                        continue;
                    }
                    codec.beginRow(child);
                    const size_t row = child.rows - 1;
                    if (!seconds.empty()) {
                        std::memcpy(child.columns[0].values.data() + row * 8, &seconds[i], 8);
                        std::memcpy(child.columns[1].values.data() + row * 4, &nanos[i], 4);
                    } else if (!texts.empty()) {
                        child.columns[0].bytes[row] = texts[i];
                    } else {
                        std::memcpy(child.columns[0].values.data() + row * width,
                            payload.data() + i * width, width);
                    }
                    codec.endRow(child);
                    target.childRow[base + i] = (int64_t)row;
                    markOneof(i);
                    has[i] = 1;
                }
                continue;
            }
            std::vector<uint8_t> missing;
            if (field.kind == ValueKind::Bytes) {
                std::vector<uint8_t> empty;
                stringVector texts = textCells(field, column, rows, empty);
                missing = flags ? missingRows(field, true, column, flags, rows) : empty;
                for (size_t i = 0; i < rows; ++i) {
                    target.bytes[base + i] = std::move(texts[i]);
                }
            } else {
                if (!(column.isNumeric() || column.isLogical()) || column.isSparse()
                    || column.isComplex()) {
                    if (!(field.type == FieldType::Enum
                            && (column.isCell() || column.isStringArray()))) {
                        Nelson::Error("protobuf:invalidValue:columnExpectsRealNumericValues",
                            "column \"%s\" expects real numeric values.", name);
                    }
                }
                missing = missingRows(field, field.hasPresence, column, flags, rows);
                const size_t width = valueSize(field.kind);
                if (column.dataClass() == nelsonTypeOf(field.kind) && !column.isSparse()
                    && !column.isComplex() && (size_t)column.elementCount() == rows) {
                    // Already in the stored type: one copy.
                    if (rows > 0) {
                        std::memcpy(target.values.data() + base * width, column.dataPointer(),
                            rows * width);
                    }
                } else if (field.type == FieldType::Enum || column.isSparse() || column.isComplex()
                    || (size_t)column.elementCount() != rows
                    || !convertFloatingColumn(
                        column, field.kind, target.values.data() + base * width, rows)) {
                    std::vector<uint8_t> payload = columnPayload(schema, field, column, rows);
                    std::memcpy(target.values.data() + base * width, payload.data(), rows * width);
                }
            }
            for (size_t i = 0; i < rows; ++i) {
                bool present = missing[i] == 0;
                if (chosen) {
                    present = selected[i] != 0;
                }
                if (!present) {
                    // Absent: back to the default value.
                    if (field.kind == ValueKind::Bytes) {
                        target.bytes[base + i].clear();
                    } else {
                        const size_t width = valueSize(field.kind);
                        std::memset(target.values.data() + (base + i) * width, 0, width);
                    }
                    continue;
                }
                if (target.hasPresence) {
                    target.presence[base + i] = 1;
                }
                markOneof(i);
                has[i] = 1;
            }
        }
        // Repeated columns not given get empty rows.
        for (size_t k = 0; k < message.fields.size(); ++k) {
            Column& column = set.columns[k];
            if (column.repeated && column.offsets.size() < set.rows + 1) {
                column.offsets.resize(set.rows + 1, column.offsets.back());
            }
        }
        return has;
    }
    //=============================================================================
    ArrayOf
    scalarValue(ValueKind kind, const uint8_t* slot)
    {
        switch (kind) {
        case ValueKind::Double: {
            double v;
            std::memcpy(&v, slot, 8);
            return ArrayOf::doubleScalar(v);
        }
        case ValueKind::Float: {
            float v;
            std::memcpy(&v, slot, 4);
            return ArrayOf::singleScalar(v);
        }
        case ValueKind::Int64: {
            int64 v;
            std::memcpy(&v, slot, 8);
            return ArrayOf::int64Scalar(v);
        }
        case ValueKind::UInt64: {
            uint64 v;
            std::memcpy(&v, slot, 8);
            return ArrayOf::uint64Scalar(v);
        }
        case ValueKind::Int32: {
            int32 v;
            std::memcpy(&v, slot, 4);
            return ArrayOf::int32Scalar(v);
        }
        case ValueKind::UInt32: {
            uint32 v;
            std::memcpy(&v, slot, 4);
            return ArrayOf::uint32Scalar(v);
        }
        case ValueKind::Bool:
            return ArrayOf::logicalScalar(*slot != 0);
        default:
            return ArrayOf::empty(0, 0);
        }
    }
    //=============================================================================
    ArrayOf
    unknownValue(const ColumnSet& set, size_t row)
    {
        const std::string empty;
        const std::string& bytes = row < set.unknown.size() ? set.unknown[row] : empty;
        void* data
            = ArrayOf::allocateArrayOf(NLS_UINT8, (indexType)bytes.size(), stringVector(), false);
        if (!bytes.empty()) {
            std::memcpy(data, bytes.data(), bytes.size());
        }
        return ArrayOf(NLS_UINT8, Dimensions(bytes.empty() ? 0 : 1, (indexType)bytes.size()), data);
    }
    //=============================================================================
    void
    addFieldPath(const Schema& schema, int32_t messageIndex, FieldSelection& selection,
        const std::string& path)
    {
        const MessageDef& message = schema.messages[(size_t)messageIndex];
        if (selection.keep.empty()) {
            selection.keep.assign(message.fields.size(), 0);
            selection.children.assign(message.fields.size(), nullptr);
        }
        const size_t dot = path.find('.');
        const std::string name = path.substr(0, dot);
        const int32_t fieldIndex = message.findFieldByName(name);
        if (fieldIndex < 0) {
            Nelson::Error("protobuf:invalidOption:unknownFieldMessage",
                "unknown field \"%s\" for message \"%s\".", name, message.fullName);
        }
        const size_t k = (size_t)fieldIndex;
        const bool whole = selection.keep[k] != 0 && !selection.children[k];
        if (dot == std::string::npos) {
            selection.keep[k] = 1;
            selection.children[k] = nullptr;
            return;
        }
        const FieldDef& field = message.fields[k];
        if (field.type != FieldType::Message || field.isMap) {
            Nelson::Error("protobuf:invalidOption:fieldMessageFieldsSelectSingular",
                "field \"%s\" of message \"%s\" has no fields to select (only singular and "
                "repeated message fields have).",
                name, message.fullName);
        }
        if (whole) {
            return;
        }
        selection.keep[k] = 1;
        if (!selection.children[k]) {
            selection.children[k] = std::make_shared<FieldSelection>();
        }
        addFieldPath(schema, field.messageIndex, *selection.children[k], path.substr(dot + 1));
    }
    //=============================================================================
    template <typename T>
    void
    storeRounded(const double value, uint8_t* slot)
    {
        T result = 0;
        if (!std::isnan(value)) {
            const double rounded = std::round(value);
            if (rounded <= (double)std::numeric_limits<T>::lowest()) {
                result = std::numeric_limits<T>::lowest();
            } else if (rounded >= (double)std::numeric_limits<T>::max()) {
                result = std::numeric_limits<T>::max();
            } else {
                result = (T)rounded;
            }
        }
        std::memcpy(slot, &result, sizeof(T));
    }
    //=============================================================================
    template <typename Source>
    void
    convertRows(const Source* in, ValueKind kind, uint8_t* out, size_t rows)
    {
        switch (kind) {
        case ValueKind::Double:
            for (size_t i = 0; i < rows; ++i) {
                const double v = (double)in[i];
                std::memcpy(out + i * 8, &v, 8);
            }
            break;
        case ValueKind::Float:
            for (size_t i = 0; i < rows; ++i) {
                const float v = (float)in[i];
                std::memcpy(out + i * 4, &v, 4);
            }
            break;
        case ValueKind::Int64:
            for (size_t i = 0; i < rows; ++i) {
                storeRounded<int64_t>((double)in[i], out + i * 8);
            }
            break;
        case ValueKind::UInt64:
            for (size_t i = 0; i < rows; ++i) {
                storeRounded<uint64_t>((double)in[i], out + i * 8);
            }
            break;
        case ValueKind::Int32:
            for (size_t i = 0; i < rows; ++i) {
                storeRounded<int32_t>((double)in[i], out + i * 4);
            }
            break;
        case ValueKind::UInt32:
            for (size_t i = 0; i < rows; ++i) {
                storeRounded<uint32_t>((double)in[i], out + i * 4);
            }
            break;
        case ValueKind::Bool:
            for (size_t i = 0; i < rows; ++i) {
                out[i] = in[i] != 0 && !std::isnan((double)in[i]) ? 1 : 0;
            }
            break;
        default:
            break;
        }
    }
    //=============================================================================
    bool
    convertFloatingColumn(const ArrayOf& column, ValueKind kind, uint8_t* out, size_t rows)
    {
        if (kind == ValueKind::Bytes || kind == ValueKind::Message) {
            return false;
        }
        if (column.dataClass() == NLS_DOUBLE) {
            convertRows(static_cast<const double*>(column.dataPointer()), kind, out, rows);
            return true;
        }
        if (column.dataClass() == NLS_SINGLE) {
            convertRows(static_cast<const float*>(column.dataPointer()), kind, out, rows);
            return true;
        }
        return false;
    }
    //============================================================================
    std::string
    presentFlagName(const MessageDef& message)
    {
        std::string name = "Present";
        while (message.findFieldByName(name) >= 0) {
            name += "_";
        }
        return name;
    }
} // namespace
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
