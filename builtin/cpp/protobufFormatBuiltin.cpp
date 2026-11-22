//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufFormatBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtoFormat.hpp"
#include "ProtobufConversion.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// txt = __protobuf_format__(id, type, value, format, protoNames, indent, delimited): format is
// 'text', 'json' or 'ndjson'; value is a struct (records) or uint8 bytes of one message (of
// length-delimited records when delimited is true). Several records give a JSON array, one
// JSON object per line in ndjson, records separated by blank lines in text.
ArrayOfVector
Nelson::ProtobufGateway::protobufFormatBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 7, 7);
    nargoutcheck(nLhs, 0, 1);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    const Protobuf::Codec& codec = *entry->codec;
    const std::string format = argIn[3].asUtf8String();
    const bool lines = format == "ndjson";
    const bool json = lines || format == "json";
    Protobuf::JsonOptions options;
    options.protoNames = argIn[4].asLogicalScalar();
    options.indent = !lines && argIn[5].asLogicalScalar();
    const bool delimited = argIn[6].asLogicalScalar();
    std::string out;
    try {
        Protobuf::ColumnSet set
            = codec.createColumnSet(entry->schema.resolveMessage(argIn[1].asUtf8String()));
        const ArrayOf& value = argIn[2];
        if (value.dataClass() == NLS_UINT8 && delimited) {
            codec.appendDelimited(set, static_cast<const uint8_t*>(value.dataPointer()),
                (size_t)value.elementCount(), false);
        } else if (value.dataClass() == NLS_UINT8) {
            codec.appendMessage(set, static_cast<const uint8_t*>(value.dataPointer()),
                (size_t)value.elementCount());
        } else {
            appendStructRows(codec, set, value);
        }
        const bool several = set.rows != 1;
        if (lines) {
            for (size_t row = 0; row < set.rows; ++row) {
                out += Protobuf::formatJson(codec, set, row, options) + "\n";
            }
        } else if (json && several) {
            out += "[";
        }
        for (size_t row = 0; !lines && row < set.rows; ++row) {
            if (json) {
                out += (row > 0 ? "," : "") + std::string(options.indent && several ? "\n" : "")
                    + Protobuf::formatJson(codec, set, row, options);
            } else {
                out += (row > 0 ? "\n" : "") + Protobuf::formatText(codec, set, row);
            }
        }
        if (json && !lines && several) {
            out += options.indent && set.rows > 0 ? "\n]" : "]";
        }
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:formatError");
    }
    ArrayOfVector retval;
    retval.push_back(ArrayOf::characterArray(out));
    return retval;
}
//=============================================================================
