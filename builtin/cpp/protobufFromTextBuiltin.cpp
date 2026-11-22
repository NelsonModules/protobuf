//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufFromTextBuiltin.hpp"
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
// [value, presence, report] = __protobuf_fromtext__(id, type, text, options): one message in the
// protobuf text format, returned like __protobuf_decode__ with the same options.
ArrayOfVector
Nelson::ProtobufGateway::protobufFromTextBuiltin(int nLhs, const ArrayOfVector& argIn)
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
        records = Protobuf::parseText(codec, set, argIn[2].asUtf8String());
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:textError");
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
