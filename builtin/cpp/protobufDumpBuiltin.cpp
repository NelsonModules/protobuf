//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDumpBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtoDump.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// Canonical texts compared with the reference goldens in the tests:
// txt = __protobuf_dump__(id): the descriptor of the loaded file;
// txt = __protobuf_dump__(id, type, bytes): the records of a length-delimited stream, one
// block per record separated by "---" lines.
ArrayOfVector
Nelson::ProtobufGateway::protobufDumpBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 1, 3);
    nargoutcheck(nLhs, 0, 1);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    std::string out;
    if (argIn.size() == 1) {
        out = Protobuf::dumpDescriptor(entry->schema);
    } else {
        if (argIn.size() != 3 || argIn[2].dataClass() != NLS_UINT8) {
            Nelson::Error(
                "protobuf:invalidBytes:expectedTypeUint8Bytes", "expected a type and uint8 bytes.");
        }
        const Protobuf::Codec& codec = *entry->codec;
        try {
            Protobuf::ColumnSet set
                = codec.createColumnSet(entry->schema.resolveMessage(argIn[1].asUtf8String()));
            const ArrayOf& bytes = argIn[2];
            codec.appendDelimited(set, static_cast<const uint8_t*>(bytes.dataPointer()),
                (size_t)bytes.elementCount(), false);
            for (size_t row = 0; row < set.rows; ++row) {
                std::string text = Protobuf::dumpRow(codec, set, row);
                if (!text.empty() && text.back() == '\n') {
                    text.pop_back();
                }
                out += (row > 0 ? "\n---\n" : "") + text;
            }
            out += "\n";
        } catch (const Protobuf::ProtoError& e) {
            raiseProtoError(e, "protobuf:decodeError");
        }
    }
    ArrayOfVector retval;
    retval.push_back(ArrayOf::characterArray(out));
    return retval;
}
//=============================================================================
