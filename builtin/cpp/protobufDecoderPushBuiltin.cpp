//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecoderPushBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "ErrorID.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// [records, skipped] = __protobuf_decoder_push__(id, bytes)
ArrayOfVector
Nelson::ProtobufGateway::protobufDecoderPushBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 2, 2);
    nargoutcheck(nLhs, 0, 2);
    std::shared_ptr<DecoderEntry> entry = requireDecoder(argIn[0].asDoubleScalar());
    const ArrayOf& bytes = argIn[1];
    if (bytes.dataClass() != NLS_UINT8 || (!bytes.isEmpty() && !bytes.isVector())) {
        Error(L"protobuf:invalidBytes:encodedDataUint8Vector",
            L"The encoded data must be a uint8 vector.");
    }
    Protobuf::StreamResult result;
    try {
        result = entry->decoder->push(
            static_cast<const uint8_t*>(bytes.dataPointer()), (size_t)bytes.elementCount());
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:decodeError");
    }
    ArrayOfVector retval;
    retval.push_back(ArrayOf::doubleScalar((double)result.records));
    if (nLhs > 1) {
        retval.push_back(ArrayOf::doubleScalar((double)result.skipped));
    }
    return retval;
}
//=============================================================================
