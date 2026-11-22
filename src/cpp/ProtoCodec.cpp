//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoCodec.hpp"
#include "ProtoWire.hpp"
#include <algorithm>
#include <iterator>
#include <cmath>
#include <cstdlib>
#include <cstring>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    constexpr int kMaxDepth = 100;
    constexpr uint64_t kMaxFieldNumber = 536870911;
    constexpr uint32_t kWireVarint = 0;
    constexpr uint32_t kWireFixed64 = 1;
    constexpr uint32_t kWireLength = 2;
    constexpr uint32_t kWireStartGroup = 3;
    constexpr uint32_t kWireEndGroup = 4;
    constexpr uint32_t kWireFixed32 = 5;
    //=============================================================================
    using Wire::readFixed32;
    using Wire::readFixed64;
    using Wire::readLength;
    using Wire::readPacked;
    using Wire::readScalar;
    using Wire::readVarint;
    void
    skipField(const uint8_t*& p, const uint8_t* end, uint32_t wireType, uint64_t number, int depth);
    uint32_t
    wireTypeOf(FieldType type);
    bool
    isValidUtf8(const uint8_t* data, size_t size);
    void
    checkUtf8(const FieldDef& field, const uint8_t* data, size_t size);
    void
    writeVarint(std::string& out, uint64_t value);
    void
    writeTag(std::string& out, int32_t number, uint32_t wireType);
    void
    writeFixed32(std::string& out, uint32_t value);
    void
    writeFixed64(std::string& out, uint64_t value);
    void
    writeScalar(std::string& out, const FieldDef& field, const Column& column, size_t index);
    bool
    isZeroSlot(const Column& column, size_t row);
    // Body of a group whose start tag was just read; p moves after the end tag.
    size_t
    groupBody(
        const uint8_t*& p, const uint8_t* end, uint64_t number, int depth, const uint8_t*& body);
    // Slot bytes of a default value given as reference descriptor text.
    void
    defaultSlotOf(const Schema& schema, const FieldDef& field, uint8_t* slot);
    // Bytes of a C-escaped default value.
    std::string
    unescapeBytes(const std::string& text);
    //=============================================================================
    template <typename T>
    inline void
    putValue(Column& column, bool append, size_t row, T value)
    {
        if (append) {
            size_t n = column.values.size();
            column.values.resize(n + sizeof(T));
            std::memcpy(column.values.data() + n, &value, sizeof(T));
        } else {
            std::memcpy(column.values.data() + row * sizeof(T), &value, sizeof(T));
        }
    }
    //=============================================================================
    template <typename T>
    inline T
    getValue(const Column& column, size_t index)
    {
        T value;
        std::memcpy(&value, column.values.data() + index * sizeof(T), sizeof(T));
        return value;
    }
    //=============================================================================
} // namespace
//=============================================================================
size_t
valueSize(ValueKind kind)
{
    switch (kind) {
    case ValueKind::Double:
    case ValueKind::Int64:
    case ValueKind::UInt64:
        return 8;
    case ValueKind::Float:
    case ValueKind::Int32:
    case ValueKind::UInt32:
        return 4;
    case ValueKind::Bool:
        return 1;
    default:
        return 0;
    }
}
//=============================================================================
size_t
Column::count() const
{
    switch (kind) {
    case ValueKind::Bytes:
        return bytes.size();
    case ValueKind::Message:
        return child ? child->rows : 0;
    default:
        return values.size() / valueSize(kind);
    }
}
//=============================================================================
Codec::Codec(const Schema& schema) : schemaRef(schema)
{
    fieldOrder.resize(schema.messages.size());
    for (size_t m = 0; m < schema.messages.size(); ++m) {
        const MessageDef& message = schema.messages[m];
        std::vector<int32_t>& order = fieldOrder[m];
        for (size_t k = 0; k < message.fields.size(); ++k) {
            order.push_back((int32_t)k);
        }
        std::sort(order.begin(), order.end(), [&message](int32_t a, int32_t b) {
            return message.fields[(size_t)a].number < message.fields[(size_t)b].number;
        });
    }
    plans.resize(schema.messages.size());
    for (size_t m = 0; m < schema.messages.size(); ++m) {
        const MessageDef& message = schema.messages[m];
        MessagePlan& plan = plans[m];
        std::vector<int32_t> slotOfOneof(message.oneofs.size(), -1);
        int32_t slots = 0;
        for (size_t k = 0; k < message.oneofs.size(); ++k) {
            if (!message.oneofs[k].synthetic) {
                slotOfOneof[k] = slots++;
            }
        }
        plan.byNumber = message.denseLookup;
        for (const FieldDef& field : message.fields) {
            FieldPlan item;
            item.type = field.type;
            item.kind = field.kind;
            item.repeated = field.label == FieldLabel::Repeated;
            item.wireType = field.kind == ValueKind::Message || field.kind == ValueKind::Bytes
                ? (field.delimited ? kWireStartGroup : kWireLength)
                : wireTypeOf(field.type);
            item.width = (uint32_t)valueSize(field.kind);
            item.messageIndex = field.messageIndex;
            item.oneofSlot = field.oneofIndex >= 0 ? slotOfOneof[(size_t)field.oneofIndex] : -1;
            item.presence = !item.repeated && field.hasPresence && field.kind != ValueKind::Message;
            item.utf8 = field.type == FieldType::String && field.utf8Check;
            item.required = field.required;
            if (field.type == FieldType::Enum && schema.enums[(size_t)field.enumIndex].closed) {
                item.closed = true;
                for (const EnumValueDef& v : schema.enums[(size_t)field.enumIndex].values) {
                    item.known.push_back(v.number);
                }
                std::sort(item.known.begin(), item.known.end());
            }
            if (!item.repeated && field.kind != ValueKind::Message) {
                if (field.kind == ValueKind::Bytes) {
                    item.hasDefault = field.hasDefault && !field.defaultValue.empty();
                    item.defaultBytes = field.type == FieldType::Bytes
                        ? unescapeBytes(field.defaultValue)
                        : field.defaultValue;
                } else {
                    defaultSlotOf(schema, field, item.defaultSlot);
                    for (uint32_t b = 0; b < item.width; ++b) {
                        item.hasDefault = item.hasDefault || item.defaultSlot[b] != 0;
                    }
                }
            }
            if (item.repeated) {
                plan.repeatedFields.push_back((int32_t)plan.fields.size());
            }
            plan.fields.push_back(item);
        }
    }
}
//=============================================================================
ColumnSet
Codec::createColumnSet(int32_t messageIndex) const
{
    if (messageIndex < 0 || (size_t)messageIndex >= schemaRef.messages.size()) {
        Error("protobuf:decodeError:invalidMessageType", "invalid message type.");
    }
    const MessageDef& message = schemaRef.messages[(size_t)messageIndex];
    ColumnSet set;
    set.messageIndex = messageIndex;
    set.columns.resize(message.fields.size());
    for (size_t k = 0; k < message.fields.size(); ++k) {
        const FieldDef& field = message.fields[k];
        Column& column = set.columns[k];
        column.kind = field.kind;
        column.repeated = field.label == FieldLabel::Repeated;
        column.hasPresence = field.hasPresence;
        if (column.repeated) {
            column.offsets.push_back(0);
        }
    }
    for (size_t k = 0; k < message.oneofs.size(); ++k) {
        if (!message.oneofs[k].synthetic) {
            set.oneofIndices.push_back((int32_t)k);
            set.oneofCase.emplace_back();
        }
    }
    return set;
}
//=============================================================================
void
Codec::appendMessage(ColumnSet& set, const uint8_t* data, size_t size) const
{
    try {
        appendOne(set, data, size);
    } catch (...) {
        finish(set);
        throw;
    }
    finish(set);
}
//=============================================================================
void
Codec::appendOne(ColumnSet& set, const uint8_t* data, size_t size) const
{
    size_t rowsBefore = set.rows;
    try {
        beginRow(set);
        decodeRow(set, data, size, 0);
        endRow(set);
    } catch (...) {
        rollback(set, rowsBefore);
        throw;
    }
}
//=============================================================================
StreamResult
Codec::appendDelimited(
    ColumnSet& set, const uint8_t* data, size_t size, bool skipInvalid, size_t firstRecord) const
{
    StreamResult result;
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    // Counting the records first lets every column be allocated once.
    size_t expected = 0;
    try {
        for (const uint8_t* q = data; q < end; ++expected) {
            q += readLength(q, end);
        }
    } catch (const ProtoError&) {
    }
    growRows(set, set.rows + expected);
    try {
        while (p < end) {
            size_t length = 0;
            try {
                length = readLength(p, end);
            } catch (const ProtoError& e) {
                if (!skipInvalid) {
                    Error("protobuf:decodeError:atRecord", "record %d: %s",
                        firstRecord + result.records + result.skipped + 1, e);
                }
                result.skipped++;
                break;
            }
            try {
                appendOne(set, p, length);
                result.records++;
            } catch (const ProtoError& e) {
                if (!skipInvalid) {
                    Error("protobuf:decodeError:atRecord", "record %d: %s",
                        firstRecord + result.records + result.skipped + 1, e);
                }
                result.skipped++;
            }
            p += length;
        }
    } catch (...) {
        finish(set);
        throw;
    }
    finish(set);
    return result;
}
//=============================================================================
void
Codec::growRows(ColumnSet& set, size_t capacity) const
{
    if (capacity <= set.capacity) {
        return;
    }
    const MessagePlan& plan = plans[(size_t)set.messageIndex];
    for (size_t k = 0; k < set.columns.size(); ++k) {
        const FieldPlan& field = plan.fields[k];
        Column& column = set.columns[k];
        if (field.repeated) {
            continue;
        }
        switch (field.kind) {
        case ValueKind::Message:
            column.childRow.resize(capacity, -1);
            break;
        case ValueKind::Bytes:
            column.bytes.resize(capacity);
            break;
        default:
            column.values.resize(capacity * field.width);
            break;
        }
        if (field.hasDefault) {
            fillDefault(field, column, set.capacity, capacity);
        }
        if (field.presence) {
            column.presence.resize(capacity, 0);
        }
    }
    for (std::vector<int32_t>& cases : set.oneofCase) {
        cases.resize(capacity, -1);
    }
    if (set.keepUnknown) {
        set.unknown.resize(capacity);
    }
    set.capacity = capacity;
}
//=============================================================================
void
Codec::finish(ColumnSet& set) const
{
    const MessagePlan& plan = plans[(size_t)set.messageIndex];
    const size_t rows = set.rows;
    for (size_t k = 0; k < set.columns.size(); ++k) {
        const FieldPlan& field = plan.fields[k];
        Column& column = set.columns[k];
        if (column.child) {
            finish(*column.child);
        }
        if (field.repeated) {
            continue;
        }
        switch (field.kind) {
        case ValueKind::Message:
            column.childRow.resize(rows);
            break;
        case ValueKind::Bytes:
            column.bytes.resize(rows);
            break;
        default:
            column.values.resize(rows * field.width);
            break;
        }
        if (field.presence) {
            column.presence.resize(rows);
        }
    }
    for (std::vector<int32_t>& cases : set.oneofCase) {
        cases.resize(rows);
    }
    if (set.unknown.size() > rows) {
        set.unknown.resize(rows);
    }
    set.capacity = rows;
}
//=============================================================================
void
Codec::appendRows(ColumnSet& set, std::vector<ColumnSet*>& parts) const
{
    finish(set);
    size_t added = 0;
    for (ColumnSet* part : parts) {
        finish(*part);
        added += part->rows;
    }
    const MessagePlan& plan = plans[(size_t)set.messageIndex];
    for (size_t k = 0; k < set.columns.size(); ++k) {
        const FieldPlan& field = plan.fields[k];
        Column& to = set.columns[k];
        // One allocation per column for all the parts.
        size_t values = to.values.size();
        size_t bytes = to.bytes.size();
        for (ColumnSet* part : parts) {
            values += part->columns[k].values.size();
            bytes += part->columns[k].bytes.size();
        }
        to.values.reserve(values);
        to.bytes.reserve(bytes);
        to.presence.reserve(field.presence ? set.rows + added : 0);
        if (field.repeated) {
            to.offsets.reserve(set.rows + added + 1);
        } else if (field.kind == ValueKind::Message) {
            to.childRow.reserve(set.rows + added);
        }
        int64_t childBase = to.child ? (int64_t)to.child->rows : 0;
        uint64_t elementBase = field.repeated ? to.offsets.back() : 0;
        std::vector<ColumnSet*> children;
        for (ColumnSet* part : parts) {
            Column& from = part->columns[k];
            if (field.repeated) {
                for (size_t j = 1; j < from.offsets.size(); ++j) {
                    to.offsets.push_back(from.offsets[j] + elementBase);
                }
                elementBase = to.offsets.back();
            } else if (field.kind == ValueKind::Message) {
                for (int64_t row : from.childRow) {
                    to.childRow.push_back(row < 0 ? -1 : row + childBase);
                }
            }
            if (from.child) {
                childBase += (int64_t)from.child->rows;
                children.push_back(from.child.get());
            }
            to.values.insert(to.values.end(), from.values.begin(), from.values.end());
            to.bytes.insert(to.bytes.end(), std::make_move_iterator(from.bytes.begin()),
                std::make_move_iterator(from.bytes.end()));
            to.presence.insert(to.presence.end(), from.presence.begin(), from.presence.end());
        }
        if (!children.empty()) {
            ColumnSet& child = childSet(to, field.messageIndex);
            appendRows(child, children);
        }
    }
    for (size_t k = 0; k < set.oneofCase.size(); ++k) {
        set.oneofCase[k].reserve(set.rows + added);
        for (ColumnSet* part : parts) {
            set.oneofCase[k].insert(
                set.oneofCase[k].end(), part->oneofCase[k].begin(), part->oneofCase[k].end());
        }
    }
    if (set.keepUnknown || !set.unknown.empty()) {
        set.unknown.resize(set.rows);
        for (ColumnSet* part : parts) {
            part->unknown.resize(part->rows);
            set.unknown.insert(set.unknown.end(), std::make_move_iterator(part->unknown.begin()),
                std::make_move_iterator(part->unknown.end()));
        }
    }
    set.rows += added;
    set.capacity = set.rows;
}
//=============================================================================
// Slots from set.rows to set.capacity always hold default values, so a new row is ready.
void
Codec::beginRow(ColumnSet& set) const
{
    if (set.rows == set.capacity) {
        growRows(set, std::max<size_t>(16, set.capacity * 2));
    }
    set.rows++;
}
//=============================================================================
void
Codec::reserveRows(ColumnSet& set, size_t rows) const
{
    growRows(set, rows);
}
//=============================================================================
void
Codec::endRow(ColumnSet& set) const
{
    for (int32_t k : plans[(size_t)set.messageIndex].repeatedFields) {
        Column& column = set.columns[(size_t)k];
        column.offsets.push_back(column.count());
    }
}
//=============================================================================
ColumnSet&
Codec::childSet(Column& column, int32_t messageIndex) const
{
    if (!column.child) {
        column.child = std::make_unique<ColumnSet>(createColumnSet(messageIndex));
        if (column.childSelection) {
            select(*column.child, column.childSelection);
        }
    }
    return *column.child;
}
//=============================================================================
void
Codec::select(ColumnSet& set, std::shared_ptr<const FieldSelection> selection) const
{
    set.selection = selection;
    set.keep = selection ? selection->keep : std::vector<uint8_t>();
    const MessageDef& message = schemaRef.messages[(size_t)set.messageIndex];
    for (size_t k = 0; k < set.columns.size(); ++k) {
        Column& column = set.columns[k];
        column.childSelection = selection && k < selection->children.size()
            ? std::shared_ptr<const FieldSelection>(selection->children[k])
            : nullptr;
        if (column.child) {
            select(*column.child, column.childSelection);
        } else if (column.childSelection) {
            childSet(column, message.fields[k].messageIndex);
        }
    }
}
//=============================================================================
ColumnSet
Codec::emptyLike(const ColumnSet& set) const
{
    ColumnSet out = createColumnSet(set.messageIndex);
    if (set.selection) {
        select(out, set.selection);
    } else {
        out.keep = set.keep;
    }
    out.keepUnknown = set.keepUnknown;
    return out;
}
//=============================================================================
ColumnSet
Codec::selectRows(const ColumnSet& set, const std::vector<uint8_t>& mask) const
{
    ColumnSet out = emptyLike(set);
    std::string bytes;
    for (size_t row = 0; row < set.rows && row < mask.size(); ++row) {
        if (mask[row] == 0) {
            continue;
        }
        bytes.clear();
        encodeRowTo(bytes, set, row, false);
        try {
            appendOne(out, reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size());
        } catch (...) {
            finish(out);
            throw;
        }
    }
    finish(out);
    return out;
}
//=============================================================================
void
Codec::appendChild(
    Column& column, int32_t messageIndex, const uint8_t* data, size_t size, int depth) const
{
    ColumnSet& child = childSet(column, messageIndex);
    beginRow(child);
    decodeRow(child, data, size, depth);
    endRow(child);
}
//=============================================================================
void
Codec::decodeRow(ColumnSet& set, const uint8_t* data, size_t size, int depth) const
{
    if (depth > kMaxDepth) {
        Error("protobuf:decodeError:messageNestingTooDeep", "message nesting is too deep.");
    }
    const MessageDef& message = schemaRef.messages[(size_t)set.messageIndex];
    const MessagePlan& plan = plans[(size_t)set.messageIndex];
    const size_t row = set.rows - 1;
    // One reusable buffer per depth avoids an allocation per record.
    static thread_local std::vector<std::vector<PendingMessage>> pendingByDepth(kMaxDepth + 2);
    std::vector<PendingMessage>& pending = pendingByDepth[(size_t)depth];
    pending.clear();
    const uint8_t* p = data;
    const uint8_t* end = data + size;
    auto skipUnknown = [&](const uint8_t* start, uint32_t wireType, uint64_t number) {
        skipField(p, end, wireType, number, depth);
        if (set.keepUnknown) {
            set.unknown[row].append(reinterpret_cast<const char*>(start), (size_t)(p - start));
        }
    };
    while (p < end) {
        const uint8_t* fieldStart = p;
        uint64_t tag = readVarint(p, end);
        uint32_t wireType = (uint32_t)(tag & 7);
        uint64_t number = tag >> 3;
        int32_t fieldIndex = number < plan.byNumber.size() ? plan.byNumber[(size_t)number] : -2;
        if (fieldIndex == -2 || number == 0) {
            if (number == 0 || number > kMaxFieldNumber) {
                Error("protobuf:decodeError:invalidFieldNumberInput",
                    "invalid field number in the input.");
            }
            fieldIndex = message.findField((int32_t)number);
        }
        if (fieldIndex < 0) {
            skipUnknown(fieldStart, wireType, number);
            continue;
        }
        if (!set.keep.empty() && set.keep[(size_t)fieldIndex] == 0) {
            skipField(p, end, wireType, number, depth);
            continue;
        }
        const FieldPlan& field = plan.fields[(size_t)fieldIndex];
        Column& column = set.columns[(size_t)fieldIndex];
        // Most frequent case: singular fixed-size value outside a real oneof.
        if (field.closed) {
            // Numbers outside a closed enum are unknown fields; known ones decode as usual.
            if (wireType == kWireVarint) {
                const uint8_t* valueStart = p;
                const int32_t value = (int32_t)readVarint(p, end);
                if (!std::binary_search(field.known.begin(), field.known.end(), value)) {
                    if (set.keepUnknown) {
                        set.unknown[row].append(
                            reinterpret_cast<const char*>(fieldStart), (size_t)(p - fieldStart));
                    }
                    continue;
                }
                p = valueStart;
            } else if (field.repeated && wireType == kWireLength) {
                const size_t length = readLength(p, end);
                const uint8_t* stop = p + length;
                while (p < stop) {
                    const int32_t value = (int32_t)readVarint(p, stop);
                    if (std::binary_search(field.known.begin(), field.known.end(), value)) {
                        putValue<int32_t>(column, true, 0, value);
                    } else if (set.keepUnknown) {
                        writeTag(set.unknown[row], (int32_t)number, kWireVarint);
                        writeVarint(set.unknown[row], (uint64_t)(int64_t)value);
                    }
                }
                continue;
            }
        }
        if (!field.repeated && field.width != 0 && field.oneofSlot < 0 && !field.closed
            && wireType == field.wireType) {
            readScalar(field.type, column.values.data() + row * field.width, p, end);
            if (field.presence) {
                column.presence[row] = 1;
            }
            continue;
        }
        if (field.repeated) {
            if (field.kind == ValueKind::Message) {
                if (wireType != field.wireType) {
                    skipUnknown(fieldStart, wireType, number);
                    continue;
                }
                childSet(column, field.messageIndex).keepUnknown = set.keepUnknown;
                if (wireType == kWireStartGroup) {
                    const uint8_t* body = nullptr;
                    const size_t length = groupBody(p, end, number, depth, body);
                    appendChild(column, field.messageIndex, body, length, depth + 1);
                    continue;
                }
                size_t length = readLength(p, end);
                appendChild(column, field.messageIndex, p, length, depth + 1);
                p += length;
            } else if (field.kind == ValueKind::Bytes) {
                if (wireType != kWireLength) {
                    skipUnknown(fieldStart, wireType, number);
                    continue;
                }
                size_t length = readLength(p, end);
                if (field.utf8) {
                    checkUtf8(message.fields[(size_t)fieldIndex], p, length);
                }
                column.bytes.emplace_back(reinterpret_cast<const char*>(p), length);
                p += length;
            } else if (wireType == kWireLength) {
                size_t length = readLength(p, end);
                readPacked(field.type, field.wireType, field.width, column.values, p, p + length);
            } else if (wireType == field.wireType) {
                const size_t n = column.values.size();
                column.values.resize(n + field.width);
                readScalar(field.type, column.values.data() + n, p, end);
            } else {
                skipUnknown(fieldStart, wireType, number);
            }
            continue;
        }
        if (wireType != field.wireType) {
            skipUnknown(fieldStart, wireType, number);
            continue;
        }
        if (field.oneofSlot >= 0) {
            std::vector<int32_t>& cases = set.oneofCase[(size_t)field.oneofSlot];
            int32_t previous = cases[row];
            if (previous >= 0 && previous != fieldIndex) {
                clearOneofMember(set, previous, row, pending);
            }
            cases[row] = fieldIndex;
        }
        if (field.kind == ValueKind::Message) {
            if (wireType == kWireStartGroup) {
                const uint8_t* body = nullptr;
                const size_t length = groupBody(p, end, number, depth, body);
                pending.push_back({ fieldIndex, body, length });
                continue;
            }
            size_t length = readLength(p, end);
            pending.push_back({ fieldIndex, p, length });
            p += length;
        } else if (field.kind == ValueKind::Bytes) {
            size_t length = readLength(p, end);
            if (field.utf8) {
                checkUtf8(message.fields[(size_t)fieldIndex], p, length);
            }
            column.bytes[row].assign(reinterpret_cast<const char*>(p), length);
            p += length;
        } else {
            readScalar(field.type, column.values.data() + row * field.width, p, end);
        }
        if (field.presence) {
            column.presence[row] = 1;
        }
    }
    // Singular sub-messages: several occurrences merge, like a concatenated input.
    for (size_t k = 0; k < pending.size(); ++k) {
        int32_t fieldIndex = pending[k].field;
        if (fieldIndex < 0) {
            continue;
        }
        const FieldPlan& field = plan.fields[(size_t)fieldIndex];
        Column& column = set.columns[(size_t)fieldIndex];
        bool several = false;
        for (size_t j = k + 1; j < pending.size(); ++j) {
            several = several || pending[j].field == fieldIndex;
        }
        childSet(column, field.messageIndex).keepUnknown = set.keepUnknown;
        if (!several) {
            appendChild(column, field.messageIndex, pending[k].data, pending[k].size, depth + 1);
        } else {
            std::string merged;
            for (size_t j = k; j < pending.size(); ++j) {
                if (pending[j].field == fieldIndex) {
                    merged.append(reinterpret_cast<const char*>(pending[j].data), pending[j].size);
                    pending[j].field = -1;
                }
            }
            appendChild(column, field.messageIndex, reinterpret_cast<const uint8_t*>(merged.data()),
                merged.size(), depth + 1);
        }
        column.childRow[row] = (int64_t)column.child->rows - 1;
    }
}
//=============================================================================
void
Codec::clearOneofMember(
    ColumnSet& set, int32_t fieldIndex, size_t row, std::vector<PendingMessage>& pending) const
{
    Column& column = set.columns[(size_t)fieldIndex];
    switch (column.kind) {
    case ValueKind::Message:
        for (PendingMessage& item : pending) {
            if (item.field == fieldIndex) {
                item.field = -1;
            }
        }
        break;
    case ValueKind::Bytes:
        column.bytes[row].clear();
        break;
    default: {
        size_t width = valueSize(column.kind);
        std::memset(column.values.data() + row * width, 0, width);
    } break;
    }
    const FieldPlan& field = plans[(size_t)set.messageIndex].fields[(size_t)fieldIndex];
    if (field.hasDefault) {
        fillDefault(field, column, row, row + 1);
    }
    if (column.hasPresence && column.kind != ValueKind::Message) {
        column.presence[row] = 0;
    }
}
//=============================================================================
void
Codec::fillDefault(const FieldPlan& field, Column& column, size_t first, size_t last) const
{
    if (field.kind == ValueKind::Bytes) {
        for (size_t r = first; r < last; ++r) {
            column.bytes[r] = field.defaultBytes;
        }
        return;
    }
    for (size_t r = first; r < last; ++r) {
        std::memcpy(column.values.data() + r * field.width, field.defaultSlot, field.width);
    }
}
//=============================================================================
void
Codec::rollback(ColumnSet& set, size_t rows) const
{
    if (rows >= set.rows) {
        return;
    }
    const MessagePlan& plan = plans[(size_t)set.messageIndex];
    const size_t used = set.rows;
    for (size_t k = 0; k < set.columns.size(); ++k) {
        const FieldPlan& field = plan.fields[k];
        Column& column = set.columns[k];
        if (field.repeated) {
            size_t keep = (size_t)column.offsets[rows];
            column.offsets.resize(rows + 1);
            if (field.kind == ValueKind::Message) {
                if (column.child) {
                    rollback(*column.child, keep);
                }
            } else if (field.kind == ValueKind::Bytes) {
                column.bytes.resize(keep);
            } else {
                column.values.resize(keep * field.width);
            }
            continue;
        }
        // Removed rows go back to default values; the capacity is kept.
        switch (field.kind) {
        case ValueKind::Message: {
            size_t keep = 0;
            for (size_t r = rows; r > 0; --r) {
                if (column.childRow[r - 1] >= 0) {
                    keep = (size_t)column.childRow[r - 1] + 1;
                    break;
                }
            }
            std::fill(column.childRow.begin() + (std::ptrdiff_t)rows,
                column.childRow.begin() + (std::ptrdiff_t)used, -1);
            if (column.child) {
                rollback(*column.child, keep);
            }
        } break;
        case ValueKind::Bytes:
            for (size_t r = rows; r < used; ++r) {
                std::string().swap(column.bytes[r]);
            }
            break;
        default:
            std::memset(column.values.data() + rows * field.width, 0, (used - rows) * field.width);
            break;
        }
        if (field.hasDefault) {
            fillDefault(field, column, rows, used);
        }
        if (field.presence) {
            std::fill(column.presence.begin() + (std::ptrdiff_t)rows,
                column.presence.begin() + (std::ptrdiff_t)used, (uint8_t)0);
        }
    }
    for (std::vector<int32_t>& cases : set.oneofCase) {
        std::fill(cases.begin() + (std::ptrdiff_t)rows, cases.begin() + (std::ptrdiff_t)used, -1);
    }
    for (size_t r = rows; r < std::min(used, set.unknown.size()); ++r) {
        std::string().swap(set.unknown[r]);
    }
    set.rows = rows;
}
//=============================================================================
void
Codec::truncate(ColumnSet& set, size_t rows) const
{
    rollback(set, rows);
    finish(set);
}
//=============================================================================
std::string
Codec::encodeRow(const ColumnSet& set, size_t row) const
{
    if (row >= set.rows) {
        Error("protobuf:decodeError:rowIndexOutRange", "row index out of range.");
    }
    std::string out;
    encodeRowTo(out, set, row);
    return out;
}
//=============================================================================
std::string
Codec::encodeDelimited(const ColumnSet& set) const
{
    std::string out;
    encodeDelimitedRows(set, 0, set.rows, out);
    return out;
}
//=============================================================================
void
Codec::encodeDelimitedRows(const ColumnSet& set, size_t first, size_t last, std::string& out) const
{
    // Each record is written in place after a one-byte size; a longer size moves it.
    for (size_t row = first; row < last && row < set.rows; ++row) {
        const size_t at = out.size();
        out.push_back('\0');
        encodeRowTo(out, set, row);
        const size_t length = out.size() - at - 1;
        if (length < 0x80) {
            out[at] = (char)length;
        } else {
            std::string header;
            writeVarint(header, length);
            out.replace(at, 1, header);
        }
    }
}
//=============================================================================
void
Codec::encodeRowTo(std::string& out, const ColumnSet& set, size_t row, bool checkRequired) const
{
    const MessageDef& message = schemaRef.messages[(size_t)set.messageIndex];
    // Map entries always carry both key and value.
    const bool writeAll = message.mapEntry;
    for (int32_t fieldIndex : fieldOrder[(size_t)set.messageIndex]) {
        const FieldDef& field = message.fields[(size_t)fieldIndex];
        const Column& column = set.columns[(size_t)fieldIndex];
        if (column.repeated) {
            size_t first = (size_t)column.offsets[row];
            size_t last = (size_t)column.offsets[row + 1];
            if (first == last) {
                continue;
            }
            if (column.kind == ValueKind::Message) {
                for (size_t k = first; k < last; ++k) {
                    encodeChildTo(out, field, *column.child, k, checkRequired);
                }
            } else if (column.kind == ValueKind::Bytes) {
                for (size_t k = first; k < last; ++k) {
                    writeTag(out, field.number, kWireLength);
                    writeVarint(out, column.bytes[k].size());
                    out += column.bytes[k];
                }
            } else if (field.packed) {
                std::string payload;
                for (size_t k = first; k < last; ++k) {
                    writeScalar(payload, field, column, k);
                }
                writeTag(out, field.number, kWireLength);
                writeVarint(out, payload.size());
                out += payload;
            } else {
                for (size_t k = first; k < last; ++k) {
                    writeTag(out, field.number, wireTypeOf(field.type));
                    writeScalar(out, field, column, k);
                }
            }
            continue;
        }
        if (column.kind == ValueKind::Message) {
            int64_t childRow = column.childRow[row];
            if (childRow < 0) {
                if (field.required && checkRequired) {
                    Error("protobuf:encodeError:requiredFieldMissing",
                        "required field \"%s\" of \"%s\" is missing.", field.name,
                        message.fullName);
                }
                continue;
            }
            encodeChildTo(out, field, *column.child, (size_t)childRow, checkRequired);
            continue;
        }
        bool present = writeAll
            || (column.hasPresence ? column.presence[row] != 0
                                   : (column.kind == ValueKind::Bytes ? !column.bytes[row].empty()
                                                                      : !isZeroSlot(column, row)));
        if (!present) {
            if (field.required && checkRequired) {
                Error("protobuf:encodeError:requiredFieldMissing",
                    "required field \"%s\" of \"%s\" is missing.", field.name, message.fullName);
            }
            continue;
        }
        if (column.kind == ValueKind::Bytes) {
            writeTag(out, field.number, kWireLength);
            writeVarint(out, column.bytes[row].size());
            out += column.bytes[row];
        } else {
            writeTag(out, field.number, wireTypeOf(field.type));
            writeScalar(out, field, column, row);
        }
    }
    if (row < set.unknown.size()) {
        out += set.unknown[row];
    }
}
//=============================================================================
void
Codec::encodeChildTo(std::string& out, const FieldDef& field, const ColumnSet& child, size_t row,
    bool checkRequired) const
{
    if (field.delimited) {
        writeTag(out, field.number, kWireStartGroup);
        encodeRowTo(out, child, row, checkRequired);
        writeTag(out, field.number, kWireEndGroup);
        return;
    }
    // In place after a one-byte size; a longer size moves the message.
    writeTag(out, field.number, kWireLength);
    const size_t at = out.size();
    out.push_back('\0');
    encodeRowTo(out, child, row, checkRequired);
    const size_t length = out.size() - at - 1;
    if (length < 0x80) {
        out[at] = (char)length;
    } else {
        std::string header;
        writeVarint(header, length);
        out.replace(at, 1, header);
    }
}
//=============================================================================
namespace {
    //=============================================================================
    void
    skipField(const uint8_t*& p, const uint8_t* end, uint32_t wireType, uint64_t number, int depth)
    {
        switch (wireType) {
        case kWireVarint:
            readVarint(p, end);
            break;
        case kWireFixed64:
            readFixed64(p, end);
            break;
        case kWireLength:
            p += readLength(p, end);
            break;
        case kWireFixed32:
            readFixed32(p, end);
            break;
        case kWireStartGroup:
            if (depth > kMaxDepth) {
                Error("protobuf:decodeError:messageNestingTooDeep", "message nesting is too deep.");
            }
            while (true) {
                uint64_t tag = readVarint(p, end);
                uint32_t innerWire = (uint32_t)(tag & 7);
                if (innerWire == kWireEndGroup) {
                    if ((tag >> 3) != number) {
                        Error("protobuf:decodeError:mismatchedEndGroupTagInput",
                            "mismatched end-group tag in the input.");
                    }
                    return;
                }
                skipField(p, end, innerWire, tag >> 3, depth + 1);
            }
        case kWireEndGroup:
            Error("protobuf:decodeError:unexpectedEndGroupTagInput",
                "unexpected end-group tag in the input.");
        default:
            Error("protobuf:decodeError:invalidWireTypeInput", "invalid wire type in the input.");
        }
    }
    //=============================================================================
    uint32_t
    wireTypeOf(FieldType type)
    {
        switch (type) {
        case FieldType::Double:
        case FieldType::Fixed64:
        case FieldType::SFixed64:
            return kWireFixed64;
        case FieldType::Float:
        case FieldType::Fixed32:
        case FieldType::SFixed32:
            return kWireFixed32;
        case FieldType::String:
        case FieldType::Bytes:
        case FieldType::Message:
            return kWireLength;
        default:
            return kWireVarint;
        }
    }
    //=============================================================================
    size_t
    groupBody(
        const uint8_t*& p, const uint8_t* end, uint64_t number, int depth, const uint8_t*& body)
    {
        if (depth > kMaxDepth) {
            Error("protobuf:decodeError:messageNestingTooDeep", "message nesting is too deep.");
        }
        body = p;
        while (true) {
            const uint8_t* tagStart = p;
            const uint64_t tag = readVarint(p, end);
            const uint32_t wireType = (uint32_t)(tag & 7);
            if (wireType == kWireEndGroup) {
                if ((tag >> 3) != number) {
                    Error("protobuf:decodeError:mismatchedEndGroupTagInput",
                        "mismatched end-group tag in the input.");
                }
                return (size_t)(tagStart - body);
            }
            skipField(p, end, wireType, tag >> 3, depth + 1);
        }
    }
    //=============================================================================
    void
    defaultSlotOf(const Schema& schema, const FieldDef& field, uint8_t* slot)
    {
        std::string text = field.hasDefault ? field.defaultValue : "";
        if (field.type == FieldType::Enum) {
            // Absent enum fields read as the first value, or as the named default.
            const EnumDef& enumDef = schema.enums[(size_t)field.enumIndex];
            int32_t number = enumDef.values.empty() ? 0 : enumDef.values[0].number;
            for (const EnumValueDef& v : enumDef.values) {
                if (field.hasDefault && v.name == text) {
                    number = v.number;
                    break;
                }
            }
            std::memcpy(slot, &number, sizeof(number));
            return;
        }
        if (text.empty()) {
            return;
        }
        switch (field.kind) {
        case ValueKind::Double:
        case ValueKind::Float: {
            const bool negative = text[0] == '-';
            const std::string body = text.substr(negative ? 1 : 0);
            double value = body == "inf" ? INFINITY
                : body == "nan"          ? NAN
                                         : std::strtod(body.c_str(), nullptr);
            value = negative ? -value : value;
            if (field.kind == ValueKind::Double) {
                std::memcpy(slot, &value, sizeof(value));
            } else {
                const float single = (float)value;
                std::memcpy(slot, &single, sizeof(single));
            }
        } break;
        case ValueKind::Int64: {
            const int64_t value = std::strtoll(text.c_str(), nullptr, 10);
            std::memcpy(slot, &value, sizeof(value));
        } break;
        case ValueKind::UInt64: {
            const uint64_t value = std::strtoull(text.c_str(), nullptr, 10);
            std::memcpy(slot, &value, sizeof(value));
        } break;
        case ValueKind::Int32: {
            const int32_t value = (int32_t)std::strtoll(text.c_str(), nullptr, 10);
            std::memcpy(slot, &value, sizeof(value));
        } break;
        case ValueKind::UInt32: {
            const uint32_t value = (uint32_t)std::strtoull(text.c_str(), nullptr, 10);
            std::memcpy(slot, &value, sizeof(value));
        } break;
        case ValueKind::Bool:
            slot[0] = text == "true" ? 1 : 0;
            break;
        default:
            break;
        }
    }
    //=============================================================================
    std::string
    unescapeBytes(const std::string& text)
    {
        std::string out;
        for (size_t k = 0; k < text.size(); ++k) {
            if (text[k] != '\\' || k + 1 == text.size()) {
                out.push_back(text[k]);
                continue;
            }
            const char c = text[++k];
            if (c >= '0' && c <= '7') {
                int value = 0;
                for (int n = 0; n < 3 && k < text.size() && text[k] >= '0' && text[k] <= '7';
                    ++n, ++k) {
                    value = value * 8 + (text[k] - '0');
                }
                --k;
                out.push_back((char)value);
                continue;
            }
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
            default:
                out.push_back(c);
                break;
            }
        }
        return out;
    }
    //=============================================================================
    bool
    isValidUtf8(const uint8_t* data, size_t size)
    {
        size_t k = 0;
        while (k < size) {
            uint8_t c = data[k];
            if (c < 0x80) {
                k++;
                continue;
            }
            size_t extra;
            uint32_t codePoint;
            if ((c & 0xE0) == 0xC0) {
                extra = 1;
                codePoint = c & 0x1F;
            } else if ((c & 0xF0) == 0xE0) {
                extra = 2;
                codePoint = c & 0x0F;
            } else if ((c & 0xF8) == 0xF0) {
                extra = 3;
                codePoint = c & 0x07;
            } else {
                return false;
            }
            if (k + extra >= size) {
                return false;
            }
            for (size_t j = 1; j <= extra; ++j) {
                if ((data[k + j] & 0xC0) != 0x80) {
                    return false;
                }
                codePoint = (codePoint << 6) | (data[k + j] & 0x3F);
            }
            if ((extra == 1 && codePoint < 0x80) || (extra == 2 && codePoint < 0x800)
                || (extra == 3 && codePoint < 0x10000) || codePoint > 0x10FFFF
                || (codePoint >= 0xD800 && codePoint <= 0xDFFF)) {
                return false;
            }
            k += extra + 1;
        }
        return true;
    }
    //=============================================================================
    void
    checkUtf8(const FieldDef& field, const uint8_t* data, size_t size)
    {
        if (field.type == FieldType::String && !isValidUtf8(data, size)) {
            Error("protobuf:decodeError:stringFieldContainsInvalidUTF",
                "string field \"%s\" contains invalid UTF-8.", field.name);
        }
    }
    //=============================================================================
    void
    writeVarint(std::string& out, uint64_t value)
    {
        // One append per value: appending byte by byte costs a capacity check each time.
        char buffer[10];
        size_t n = 0;
        while (value >= 0x80) {
            buffer[n++] = (char)((value & 0x7F) | 0x80);
            value >>= 7;
        }
        buffer[n++] = (char)value;
        out.append(buffer, n);
    }
    //=============================================================================
    void
    writeTag(std::string& out, int32_t number, uint32_t wireType)
    {
        writeVarint(out, ((uint64_t)(uint32_t)number << 3) | wireType);
    }
    //=============================================================================
    void
    writeFixed32(std::string& out, uint32_t value)
    {
        char buffer[4];
        for (int k = 0; k < 4; ++k) {
            buffer[k] = (char)((value >> (8 * k)) & 0xFF);
        }
        out.append(buffer, 4);
    }
    //=============================================================================
    void
    writeFixed64(std::string& out, uint64_t value)
    {
        char buffer[8];
        for (int k = 0; k < 8; ++k) {
            buffer[k] = (char)((value >> (8 * k)) & 0xFF);
        }
        out.append(buffer, 8);
    }
    //=============================================================================
    void
    writeScalar(std::string& out, const FieldDef& field, const Column& column, size_t index)
    {
        switch (field.type) {
        case FieldType::Double: {
            double value = getValue<double>(column, index);
            uint64_t bits;
            std::memcpy(&bits, &value, 8);
            writeFixed64(out, bits);
        } break;
        case FieldType::Float: {
            float value = getValue<float>(column, index);
            uint32_t bits;
            std::memcpy(&bits, &value, 4);
            writeFixed32(out, bits);
        } break;
        case FieldType::Int64:
            writeVarint(out, (uint64_t)getValue<int64_t>(column, index));
            break;
        case FieldType::UInt64:
            writeVarint(out, getValue<uint64_t>(column, index));
            break;
        case FieldType::Int32:
        case FieldType::Enum:
            writeVarint(out, (uint64_t)(int64_t)getValue<int32_t>(column, index));
            break;
        case FieldType::Fixed64:
            writeFixed64(out, getValue<uint64_t>(column, index));
            break;
        case FieldType::SFixed64:
            writeFixed64(out, (uint64_t)getValue<int64_t>(column, index));
            break;
        case FieldType::Fixed32:
            writeFixed32(out, getValue<uint32_t>(column, index));
            break;
        case FieldType::SFixed32:
            writeFixed32(out, (uint32_t)getValue<int32_t>(column, index));
            break;
        case FieldType::Bool:
            writeVarint(out, getValue<uint8_t>(column, index) ? 1 : 0);
            break;
        case FieldType::UInt32:
            writeVarint(out, getValue<uint32_t>(column, index));
            break;
        case FieldType::SInt32: {
            int32_t n = getValue<int32_t>(column, index);
            writeVarint(out, (uint32_t)(((uint32_t)n << 1) ^ (uint32_t)(n >> 31)));
        } break;
        case FieldType::SInt64: {
            int64_t n = getValue<int64_t>(column, index);
            writeVarint(out, ((uint64_t)n << 1) ^ (uint64_t)(n >> 63));
        } break;
        default:
            Error("protobuf:decodeError:unexpectedScalarType", "unexpected scalar type.");
        }
    }
    //=============================================================================
    bool
    isZeroSlot(const Column& column, size_t row)
    {
        size_t width = valueSize(column.kind);
        const uint8_t* slot = column.values.data() + row * width;
        for (size_t k = 0; k < width; ++k) {
            if (slot[k] != 0) {
                return false;
            }
        }
        return true;
    }
    //=============================================================================
} // namespace
//=============================================================================
namespace Wire {
    //=============================================================================
    uint64_t
    readVarintSlow(const uint8_t*& p, const uint8_t* end)
    {
        uint64_t value = 0;
        if (end - p >= 10) {
            for (int shift = 0; shift < 70; shift += 7) {
                const uint8_t byte = *p++;
                value |= (uint64_t)(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) {
                    return value;
                }
            }
            Error("protobuf:decodeError:malformedVarintInput", "malformed varint in the input.");
        }
        for (int shift = 0; shift < 70; shift += 7) {
            if (p >= end) {
                Error(
                    "protobuf:decodeError:truncatedVarintInput", "truncated varint in the input.");
            }
            const uint8_t byte = *p++;
            value |= (uint64_t)(byte & 0x7F) << shift;
            if ((byte & 0x80) == 0) {
                return value;
            }
        }
        Error("protobuf:decodeError:malformedVarintInput", "malformed varint in the input.");
    }
    //=============================================================================
} // namespace Wire
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
