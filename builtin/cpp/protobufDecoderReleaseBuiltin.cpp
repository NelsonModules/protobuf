//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecoderReleaseBuiltin.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// __protobuf_decoder_release__(id)
ArrayOfVector
Nelson::ProtobufGateway::protobufDecoderReleaseBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 1, 1);
    nargoutcheck(nLhs, 0, 0);
    releaseDecoder(argIn[0].asDoubleScalar());
    return { };
}
//=============================================================================
