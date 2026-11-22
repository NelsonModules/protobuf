//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufInspectBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "i18n.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtoInspect.hpp"
#include "ProtoStream.hpp"
#include <cstring>
//=============================================================================
using namespace Nelson;
using namespace Nelson::Protobuf;
//=============================================================================
static ArrayOf
rawFieldsValue(const std::vector<RawField>& fields);
static bool
isUtf8Text(const std::string& bytes);
//=============================================================================
// [txt, s] = __protobuf_inspect__(bytes, delimited): protoc --decode_raw text, and a struct array
// (Number, Wire, Value, Text, Fields) per message; a 1xN cell of them for a delimited stream.
ArrayOfVector
Nelson::ProtobufGateway::protobufInspectBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 2, 2);
    nargoutcheck(nLhs, 0, 2);
    const ArrayOf& bytes = argIn[0];
    if (bytes.dataClass() != NLS_UINT8 || (!bytes.isEmpty() && !bytes.isVector())) {
        Error(L"protobuf:invalidBytes:encodedDataUint8Vector",
            L"The encoded data must be a uint8 vector.");
    }
    const uint8_t* data = static_cast<const uint8_t*>(bytes.dataPointer());
    const size_t size = (size_t)bytes.elementCount();
    std::vector<std::vector<RawField>> messages;
    std::string text;
    if (argIn[1].asLogicalScalar()) {
        size_t position = 0;
        while (position < size) {
            size_t length = 0;
            try {
                const size_t one = skipRecords(data + position, size - position, 1);
                length = one;
            } catch (const ProtoError&) {
                ProtobufGateway::raiseProtoError(
                    Protobuf::Message("protobuf:decodeError:atRecord", "record %d: %s",
                        messages.size() + 1,
                        Protobuf::Message("protobuf:decodeError:truncatedLengthDelimitedFieldInput",
                            "truncated length-delimited field in the input.")),
                    "protobuf:decodeError");
            }
            size_t header = 0;
            while (data[position + header] & 0x80) {
                header++;
            }
            header++;
            std::vector<RawField> fields;
            if (!parseRawMessage(data + position + header, length - header, fields)) {
                Nelson::Error("protobuf:decodeError:recordValidProtobufMessage",
                    "record %d: not a valid protobuf message.", messages.size() + 1);
            }
            text += formatI18nMessage(_("# record %d"), std::to_string(messages.size() + 1)) + "\n"
                + rawText(fields);
            messages.push_back(std::move(fields));
            position += length;
        }
    } else {
        std::vector<RawField> fields;
        if (!parseRawMessage(data, size, fields)) {
            Nelson::Error(
                "protobuf:decodeError:validProtobufMessage", "not a valid protobuf message.");
        }
        text = rawText(fields);
        messages.push_back(std::move(fields));
    }
    ArrayOfVector retval;
    retval.push_back(ArrayOf::characterArray(text));
    if (nLhs > 1) {
        if (argIn[1].asLogicalScalar()) {
            ArrayOf* cells = static_cast<ArrayOf*>(ArrayOf::allocateArrayOf(
                NLS_CELL_ARRAY, (indexType)messages.size(), stringVector(), false));
            for (size_t k = 0; k < messages.size(); ++k) {
                cells[k] = rawFieldsValue(messages[k]);
            }
            retval.push_back(
                ArrayOf(NLS_CELL_ARRAY, Dimensions(1, (indexType)messages.size()), cells));
        } else {
            retval.push_back(rawFieldsValue(messages[0]));
        }
    }
    return retval;
}
//=============================================================================
static ArrayOf
rawFieldsValue(const std::vector<RawField>& fields)
{
    stringVector names = { "Number", "Wire", "Value", "Text", "Fields" };
    if (fields.empty()) {
        Dimensions dims(1, 0);
        return ArrayOf::emptyStruct(names, dims);
    }
    static const char* wires[] = { "varint", "fixed64", "bytes", "group", "", "fixed32" };
    ArrayOf* elements = static_cast<ArrayOf*>(
        ArrayOf::allocateArrayOf(NLS_STRUCT_ARRAY, (indexType)fields.size(), names, false));
    for (size_t k = 0; k < fields.size(); ++k) {
        const RawField& field = fields[k];
        ArrayOf* element = elements + k * names.size();
        element[0] = ArrayOf::doubleScalar((double)field.number);
        element[1] = ArrayOf::characterArray(wires[field.wireType]);
        if (field.wireType == 2) {
            void* data = ArrayOf::allocateArrayOf(
                NLS_UINT8, (indexType)field.bytes.size(), stringVector(), false);
            if (!field.bytes.empty()) {
                std::memcpy(data, field.bytes.data(), field.bytes.size());
            }
            element[2] = ArrayOf(NLS_UINT8,
                Dimensions(field.bytes.empty() ? 0 : 1, (indexType)field.bytes.size()), data);
            element[3]
                = ArrayOf::characterArray(isUtf8Text(field.bytes) ? field.bytes : std::string());
        } else if (field.wireType == 3) {
            element[2] = ArrayOf::empty(0, 0);
            element[3] = ArrayOf::characterArray(std::string());
        } else {
            element[2] = field.wireType == 5 ? ArrayOf::uint32Scalar((uint32)field.value)
                                             : ArrayOf::uint64Scalar(field.value);
            element[3] = ArrayOf::characterArray(std::string());
        }
        element[4] = field.isMessage ? rawFieldsValue(field.fields) : ArrayOf::empty(0, 0);
    }
    return ArrayOf(
        NLS_STRUCT_ARRAY, Dimensions(1, (indexType)fields.size()), elements, false, names);
}
//=============================================================================
// Valid UTF-8 with no control character other than tab, newline and carriage return.
static bool
isUtf8Text(const std::string& bytes)
{
    size_t k = 0;
    while (k < bytes.size()) {
        const unsigned char c = (unsigned char)bytes[k];
        if (c < 0x80) {
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                return false;
            }
            k++;
            continue;
        }
        size_t extra = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : 0;
        if (extra == 0 || k + extra >= bytes.size()) {
            return false;
        }
        for (size_t j = 1; j <= extra; ++j) {
            if (((unsigned char)bytes[k + j] & 0xC0) != 0x80) {
                return false;
            }
        }
        k += extra + 1;
    }
    return true;
}
//=============================================================================
