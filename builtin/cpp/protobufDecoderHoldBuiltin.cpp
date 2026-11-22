//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecoderHoldBuiltin.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufConversion.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// [columns, rows] = __protobuf_decoder_hold__(id, options): the rows decoded since the last
// call wait for __protobuf_decoder_keep__; their columns are given to the 'Where' condition.
ArrayOfVector
Nelson::ProtobufGateway::protobufDecoderHoldBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 2, 2);
    nargoutcheck(nLhs, 0, 2);
    std::shared_ptr<DecoderEntry> entry = requireDecoder(argIn[0].asDoubleScalar());
    DecodeSettings settings = decodeSettings(argIn[1]);
    settings.columns = true;
    entry->filtering = true;
    entry->held = entry->decoder->take();
    ArrayOf presence;
    ArrayOfVector retval;
    retval.push_back(rowsToValue(*entry->schema->codec, entry->held, settings, presence));
    if (nLhs > 1) {
        retval.push_back(ArrayOf::doubleScalar((double)entry->held.rows));
    }
    return retval;
}
//=============================================================================
