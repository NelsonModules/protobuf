//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecodeBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufConversion.hpp"
#include "ProtobufDecode.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// [value, presence, report] = __protobuf_decode__(id, type, bytes, options): options is the
// struct of protobuf.internal.decodeBytes (Delimited, Layout, Skip, MissingAsNaN,
// WellKnownTypes, Fields, Records = [first last] or [], Workers).
ArrayOfVector
Nelson::ProtobufGateway::protobufDecodeBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 4, 4);
    nargoutcheck(nLhs, 0, 3);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    std::string typeName = argIn[1].asUtf8String();
    const ArrayOf& bytes = argIn[2];
    if (bytes.dataClass() != NLS_UINT8 || (!bytes.isEmpty() && !bytes.isVector())) {
        Error(L"protobuf:invalidBytes:encodedDataUint8Vector",
            L"The encoded data must be a uint8 vector.");
    }
    const uint8_t* data = static_cast<const uint8_t*>(bytes.dataPointer());
    return decodeData(nLhs, *entry, typeName, data, (size_t)bytes.elementCount(),
        decodeSettings(argIn[3]), nullptr);
}
//=============================================================================
