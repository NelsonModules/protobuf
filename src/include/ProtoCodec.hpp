//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoSchema.hpp"
#include <cstring>
#include <memory>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
struct ColumnSet;
//=============================================================================
// Fields decoded in a message (keep, empty for all) and, per field index, the selection of
// its sub-messages (null: every field). Built for 'Fields' options such as "a.b".
struct FieldSelection
{
    std::vector<uint8_t> keep;
    std::vector<std::shared_ptr<FieldSelection>> children;
};
//=============================================================================
// Values of one field across all rows of a ColumnSet.
struct Column
{
    ValueKind kind = ValueKind::Int32;
    bool repeated = false;
    bool hasPresence = false;
    // Numeric and bool values in their natural C type (bool as uint8_t).
    std::vector<uint8_t> values;
    // String and bytes values.
    std::vector<std::string> bytes;
    // Singular field with explicit presence: 1 when set, per row.
    std::vector<uint8_t> presence;
    // Repeated field: rows + 1 boundaries into values, bytes or child rows.
    std::vector<uint64_t> offsets;
    // Singular message field: child row per row, -1 when absent.
    std::vector<int64_t> childRow;
    // Message field: decoded sub-messages, created on first use.
    std::unique_ptr<ColumnSet> child;
    // Fields to decode in the sub-messages (null: all).
    std::shared_ptr<const FieldSelection> childSelection;
    size_t
    count() const;
    template <typename T>
    const T*
    data() const
    {
        return reinterpret_cast<const T*>(values.data());
    }
};
//=============================================================================
// Decoded messages of one type, stored column by column.
struct ColumnSet
{
    int32_t messageIndex = -1;
    size_t rows = 0;
    // Rows allocated in singular columns while decoding; finish() brings it back to rows.
    size_t capacity = 0;
    std::vector<Column> columns;
    // Per real oneof: index of the set field per row, -1 when none.
    std::vector<std::vector<int32_t>> oneofCase;
    // Oneof index in MessageDef::oneofs for each entry of oneofCase.
    std::vector<int32_t> oneofIndices;
    // Fields decoded (1) or skipped like unknown fields (0); empty keeps every field. Set with
    // Codec::select, which also passes the selection on to sub-messages.
    std::vector<uint8_t> keep;
    std::shared_ptr<const FieldSelection> selection;
    // Wire bytes of the fields not in the schema, per row, kept when keepUnknown is true and
    // written back after the known fields when encoding (rows beyond its size have none).
    bool keepUnknown = false;
    std::vector<std::string> unknown;
};
//=============================================================================
struct StreamResult
{
    size_t records = 0;
    size_t skipped = 0;
};
//=============================================================================
size_t
valueSize(ValueKind kind);
//=============================================================================
class Codec
{
public:
    explicit Codec(const Schema& schema);
    const Schema&
    schema() const
    {
        return schemaRef;
    }
    ColumnSet
    createColumnSet(int32_t messageIndex) const;
    // Append one decoded message as a new row; the set is unchanged on error.
    void
    appendMessage(ColumnSet& set, const uint8_t* data, size_t size) const;
    // Append length-delimited messages (varint size + message); error messages number the
    // records from firstRecord + 1.
    StreamResult
    appendDelimited(ColumnSet& set, const uint8_t* data, size_t size, bool skipInvalid,
        size_t firstRecord = 0) const;
    // Encode one row.
    std::string
    encodeRow(const ColumnSet& set, size_t row) const;
    // Encode every row, each prefixed by its varint size.
    std::string
    encodeDelimited(const ColumnSet& set) const;
    // Rows first to last - 1, each prefixed by its varint size, appended to out.
    void
    encodeDelimitedRows(const ColumnSet& set, size_t first, size_t last, std::string& out) const;
    void
    truncate(ColumnSet& set, size_t rows) const;
    // Fits every column to the number of rows (after beginRow calls made by the caller).
    void
    finish(ColumnSet& set) const;
    // Moves the rows of sets of the same message type after the rows of set, in order.
    void
    appendRows(ColumnSet& set, std::vector<ColumnSet*>& parts) const;
    // Row building for callers that fill columns themselves (encoding from host values).
    // beginRow adds a row of defaults; endRow closes the repeated fields of that row.
    void
    beginRow(ColumnSet& set) const;
    void
    endRow(ColumnSet& set) const;
    // Room for rows rows in singular columns, so that beginRow does not grow them one by one.
    void
    reserveRows(ColumnSet& set, size_t rows) const;
    // Sub-message rows of a message column, created on first use.
    ColumnSet&
    childSet(Column& column, int32_t messageIndex) const;
    // Fields to decode in set and its sub-messages (null: all); selected sub-message sets are
    // created at once so that every block has the same columns.
    void
    select(ColumnSet& set, std::shared_ptr<const FieldSelection> selection) const;
    // Set with no row, the same message type, selection and unknown-field choice as set.
    ColumnSet
    emptyLike(const ColumnSet& set) const;
    // Rows whose mask entry is not zero, in order (copied through their encoding).
    ColumnSet
    selectRows(const ColumnSet& set, const std::vector<uint8_t>& mask) const;

private:
    const Schema& schemaRef;
    // Field indices sorted by field number, per message.
    std::vector<std::vector<int32_t>> fieldOrder;
    // Decoding facts of one field, computed once per schema.
    struct FieldPlan
    {
        FieldType type = FieldType::Int32;
        ValueKind kind = ValueKind::Int32;
        uint32_t wireType = 0; // wire type of one unpacked value
        uint32_t width = 0; // bytes per value, 0 for text and messages
        int32_t messageIndex = -1;
        int32_t oneofSlot = -1; // index into ColumnSet::oneofCase, -1 outside a real oneof
        bool repeated = false;
        bool presence = false; // singular non-message field with a presence flag
        bool utf8 = false;
        bool required = false; // legacy required: encoding fails when absent
        // Closed enum: values not in known (sorted) are kept as unknown fields.
        bool closed = false;
        std::vector<int32_t> known;
        // Value of an absent singular field when it is not zero (editions defaults).
        bool hasDefault = false;
        uint8_t defaultSlot[8] = { };
        std::string defaultBytes;
    };
    struct MessagePlan
    {
        std::vector<FieldPlan> fields;
        // Field index by field number (-1 when unknown); larger numbers use MessageDef.
        std::vector<int32_t> byNumber;
        // Repeated fields, whose row boundaries endRow records.
        std::vector<int32_t> repeatedFields;
    };
    std::vector<MessagePlan> plans;
    struct PendingMessage
    {
        int32_t field;
        const uint8_t* data;
        size_t size;
    };
    void
    decodeRow(ColumnSet& set, const uint8_t* data, size_t size, int depth) const;
    // Decodes one message as a new row, rolled back on error; columns keep their capacity.
    void
    appendOne(ColumnSet& set, const uint8_t* data, size_t size) const;
    // Grows singular columns to capacity rows, filled with default values.
    void
    growRows(ColumnSet& set, size_t capacity) const;
    // Removes the rows after rows, keeping the capacity.
    void
    rollback(ColumnSet& set, size_t rows) const;
    void
    appendChild(
        Column& column, int32_t messageIndex, const uint8_t* data, size_t size, int depth) const;
    void
    clearOneofMember(
        ColumnSet& set, int32_t fieldIndex, size_t row, std::vector<PendingMessage>& pending) const;
    // checkRequired: legacy required fields must be set (off when copying selected rows).
    void
    encodeRowTo(
        std::string& out, const ColumnSet& set, size_t row, bool checkRequired = true) const;
    // Writes one sub-message row of field in place (tag, size or group tags, fields).
    void
    encodeChildTo(std::string& out, const FieldDef& field, const ColumnSet& child, size_t row,
        bool checkRequired) const;
    // Puts the default value of a singular field in the slots [first, last).
    void
    fillDefault(const FieldPlan& field, Column& column, size_t first, size_t last) const;
};
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
