//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufFieldsBuiltin.hpp"
#include "Error.hpp"
#include "NelsonException.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufRegistry.hpp"
#include "ProtobufConversion.hpp"
#include "ProtoFormat.hpp"
//=============================================================================
using namespace Nelson;
using namespace Nelson::Protobuf;
//=============================================================================
// Value of a field option read as a field of google.protobuf.FieldOptions ([] when unknown).
static ArrayOf
decodedOption(const Codec& codec, int32_t optionsIndex, const std::string& scope,
    const std::string& name, const std::string& literal);
//=============================================================================
// s = __protobuf_fields__(schemaId, type): struct of Nx1 columns describing the fields.
ArrayOfVector
Nelson::ProtobufGateway::protobufFieldsBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 2, 2);
    nargoutcheck(nLhs, 0, 1);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    int32_t messageIndex = -1;
    try {
        messageIndex = entry->schema.resolveMessage(argIn[1].asUtf8String());
    } catch (const ProtoError& e) {
        raiseProtoError(e, "protobuf:decodeError");
    }
    const MessageDef& message = entry->schema.messages[(size_t)messageIndex];
    const size_t n = message.fields.size();
    stringVector names, types, typeNames, oneofs, jsonNames, defaults;
    double* numbers = static_cast<double*>(
        ArrayOf::allocateArrayOf(NLS_DOUBLE, (indexType)n, stringVector(), false));
    logical* repeated = static_cast<logical*>(
        ArrayOf::allocateArrayOf(NLS_LOGICAL, (indexType)n, stringVector(), false));
    logical* maps = static_cast<logical*>(
        ArrayOf::allocateArrayOf(NLS_LOGICAL, (indexType)n, stringVector(), false));
    logical* presence = static_cast<logical*>(
        ArrayOf::allocateArrayOf(NLS_LOGICAL, (indexType)n, stringVector(), false));
    logical* required = static_cast<logical*>(
        ArrayOf::allocateArrayOf(NLS_LOGICAL, (indexType)n, stringVector(), false));
    for (size_t k = 0; k < n; ++k) {
        const FieldDef& field = message.fields[k];
        names.push_back(field.name);
        types.push_back(fieldTypeName(field.type));
        typeNames.push_back(field.typeName.empty() ? std::string() : field.typeName.substr(1));
        const bool realOneof
            = field.oneofIndex >= 0 && !message.oneofs[(size_t)field.oneofIndex].synthetic;
        oneofs.push_back(realOneof ? message.oneofs[(size_t)field.oneofIndex].name : std::string());
        jsonNames.push_back(field.jsonName);
        numbers[k] = (double)field.number;
        repeated[k] = field.label == FieldLabel::Repeated;
        maps[k] = field.isMap;
        presence[k] = field.hasPresence;
        required[k] = field.required;
        // Value an absent field stands for: the declared default, or a non-zero first enum value.
        std::string defaultText = field.hasDefault ? field.defaultValue : std::string();
        if (!field.hasDefault && field.type == FieldType::Enum
            && field.label != FieldLabel::Repeated) {
            const EnumDef& enumDef = entry->schema.enums[(size_t)field.enumIndex];
            if (!enumDef.values.empty() && enumDef.values[0].number != 0) {
                defaultText = enumDef.values[0].name;
            }
        }
        defaults.push_back(defaultText);
    }
    Dimensions column((indexType)n, 1);
    ArrayOfVector values;
    values.push_back(ArrayOf::stringArray(names, column));
    values.push_back(ArrayOf(NLS_DOUBLE, column, numbers));
    values.push_back(ArrayOf::stringArray(types, column));
    values.push_back(ArrayOf::stringArray(typeNames, column));
    values.push_back(ArrayOf(NLS_LOGICAL, column, repeated));
    values.push_back(ArrayOf(NLS_LOGICAL, column, maps));
    values.push_back(ArrayOf(NLS_LOGICAL, column, presence));
    values.push_back(ArrayOf::stringArray(oneofs, column));
    values.push_back(ArrayOf::stringArray(jsonNames, column));
    values.push_back(ArrayOf(NLS_LOGICAL, column, required));
    values.push_back(ArrayOf::stringArray(defaults, column));
    // Options: per field, a struct array of the other options: Name and Value as text, and
    // Decoded, the value with the type of the option when the schema defines it.
    const int32_t optionsIndex = entry->schema.findMessage("google.protobuf.FieldOptions");
    ArrayOf* optionCells = static_cast<ArrayOf*>(
        ArrayOf::allocateArrayOf(NLS_CELL_ARRAY, (indexType)n, stringVector(), false));
    for (size_t k = 0; k < n; ++k) {
        const auto& options = message.fields[k].options;
        stringVector optionNames;
        stringVector optionValues;
        for (const auto& option : options) {
            optionNames.push_back(option.first);
            optionValues.push_back(option.second);
        }
        const Dimensions dims(1, (indexType)options.size());
        stringVector names = { "Name", "Value", "Decoded" };
        ArrayOf* elements = static_cast<ArrayOf*>(
            ArrayOf::allocateArrayOf(NLS_STRUCT_ARRAY, (indexType)options.size(), names, false));
        const auto& literals = message.fields[k].optionLiterals;
        for (size_t j = 0; j < options.size(); ++j) {
            elements[3 * j]
                = ArrayOf::stringArray(stringVector { optionNames[j] }, Dimensions(1, 1));
            elements[3 * j + 1]
                = ArrayOf::stringArray(stringVector { optionValues[j] }, Dimensions(1, 1));
            elements[3 * j + 2] = optionsIndex < 0 || j >= literals.size()
                ? ArrayOf::empty()
                : decodedOption(
                      *entry->codec, optionsIndex, message.fullName, optionNames[j], literals[j]);
        }
        optionCells[k] = ArrayOf(NLS_STRUCT_ARRAY, dims, elements, false, names);
    }
    values.push_back(ArrayOf(NLS_CELL_ARRAY, column, optionCells));
    // Extension: full name of an extension field, "" for a field of the message.
    stringVector extensionNames;
    for (size_t k = 0; k < n; ++k) {
        extensionNames.push_back(message.fields[k].extension);
    }
    values.push_back(ArrayOf::stringArray(extensionNames, column));
    stringVector fieldNames = { "Name", "Number", "Type", "TypeName", "Repeated", "Map", "Presence",
        "Oneof", "JsonName", "Required", "Default", "Options", "Extension" };
    ArrayOfVector retval;
    retval.push_back(ArrayOf::structScalar(fieldNames, values));
    return retval;
}
//=============================================================================
ArrayOf
decodedOption(const Codec& codec, int32_t optionsIndex, const std::string& scope,
    const std::string& name, const std::string& literal)
{
    const Schema& schema = codec.schema();
    const MessageDef& options = schema.messages[(size_t)optionsIndex];
    // Head ("(units)" or "deprecated") and the sub-field path after it.
    std::string head = name;
    std::string rest;
    const size_t close = name[0] == '(' ? name.find(')') : name.find('.');
    if (close != std::string::npos) {
        head = name.substr(0, name[0] == '(' ? close + 1 : close);
        rest = name.substr(head.size());
        if (!rest.empty() && rest[0] == '.') {
            rest.erase(0, 1);
        }
    }
    std::string member;
    std::string text;
    if (head[0] == '(') {
        // Extension name, looked up from the innermost scope outwards.
        std::string written = head.substr(1, head.size() - 2);
        std::vector<std::string> candidates;
        if (!written.empty() && written[0] == '.') {
            candidates.push_back(written.substr(1));
        } else {
            std::string outer = scope;
            while (true) {
                candidates.push_back(outer.empty() ? written : outer + "." + written);
                if (outer.empty()) {
                    break;
                }
                const size_t dot = outer.find_last_of('.');
                outer = dot == std::string::npos ? "" : outer.substr(0, dot);
            }
        }
        for (const std::string& candidate : candidates) {
            for (const FieldDef& f : options.fields) {
                if (member.empty() && f.extension == candidate) {
                    member = f.name;
                    text = "[" + candidate + "]";
                }
            }
        }
    } else if (options.findFieldByName(head) >= 0) {
        member = head;
        text = head;
    }
    if (member.empty()) {
        return ArrayOf::empty();
    }
    std::vector<std::string> path;
    for (size_t start = 0; !rest.empty() && start <= rest.size();) {
        const size_t dot = rest.find('.', start);
        path.push_back(rest.substr(start, dot - start));
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
    }
    for (const std::string& part : path) {
        text += " { " + part;
    }
    text += ": " + literal;
    for (size_t k = 0; k < path.size(); ++k) {
        text += " }";
    }
    try {
        ColumnSet set = codec.createColumnSet(optionsIndex);
        parseText(codec, set, text);
        ArrayOf value = ProtobufGateway::rowsToStructArray(codec, set, true, false, false);
        value = value.field(member);
        for (const std::string& part : path) {
            value = value.field(part);
        }
        return value;
    } catch (const ProtoError&) {
        return ArrayOf::empty();
    } catch (const NelsonException&) {
        return ArrayOf::empty();
    }
}
//=============================================================================
