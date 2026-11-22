//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufFromJsonBuiltin.hpp"
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
// [value, presence, report] = __protobuf_fromjson__(id, type, text, options): a JSON object (one
// record) or array of objects, returned like __protobuf_decode__ with the same options.
ArrayOfVector
Nelson::ProtobufGateway::protobufFromJsonBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 4, 4);
    nargoutcheck(nLhs, 0, 3);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    const Protobuf::Codec& codec = *entry->codec;
    const DecodeSettings settings = decodeSettings(argIn[3]);
    Protobuf::ColumnSet set;
    size_t records = 0;
    try {
        set = codec.createColumnSet(entry->schema.resolveMessage(argIn[1].asUtf8String()));
        codec.select(set, fieldSelection(entry->schema, set.messageIndex, settings.fields));
        const ArrayOf& options = argIn[3];
        const bool lines = options.isField("NDJSON") && options.field("NDJSON").asLogicalScalar();
        records = lines ? Protobuf::parseJsonLines(codec, set, argIn[2].asUtf8String())
                        : Protobuf::parseJson(codec, set, argIn[2].asUtf8String());
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:jsonError");
    }
    ArrayOfVector retval;
    ArrayOf presence;
    retval.push_back(rowsToValue(codec, set, settings, presence));
    if (nLhs > 1) {
        retval.push_back(presence);
    }
    if (nLhs > 2) {
        retval.push_back(
            decodeReport(entry->schema.messages[(size_t)set.messageIndex].fullName, records, 0));
    }
    return retval;
}
//=============================================================================
