//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecoderCreateBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufConversion.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// id = __protobuf_decoder_create__(schemaId, type, options): Skip and Fields of the options.
ArrayOfVector
Nelson::ProtobufGateway::protobufDecoderCreateBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 3, 3);
    nargoutcheck(nLhs, 0, 1);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    std::string typeName = argIn[1].asUtf8String();
    const DecodeSettings settings = decodeSettings(argIn[2]);
    uint64_t id = 0;
    try {
        int32_t messageIndex = entry->schema.resolveMessage(typeName);
        std::shared_ptr<const Protobuf::FieldSelection> selection
            = fieldSelection(entry->schema, messageIndex, settings.fields);
        id = registerDecoder(entry, messageIndex, settings.skipInvalid);
        requireDecoder((double)id)->decoder->setSelection(selection);
        requireDecoder((double)id)->decoder->setKeepUnknown(settings.keepUnknown);
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:decodeError");
    }
    ArrayOfVector retval;
    retval.push_back(ArrayOf::doubleScalar((double)id));
    return retval;
}
//=============================================================================
