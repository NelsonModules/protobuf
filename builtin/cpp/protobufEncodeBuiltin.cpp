//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufEncodeBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "ErrorID.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufConversion.hpp"
#include "ProtobufRegistry.hpp"
#include "ProtoStream.hpp"
#include <cstring>
//=============================================================================
using namespace Nelson;
//=============================================================================
// bytes = __protobuf_encode__(id, type, value, delimited[, columns, presence, workers]): with
// columns true, value is a struct of Nx1 columns and presence the logical columns of decoding;
// workers threads encode delimited records (0 chooses from the record count).
ArrayOfVector
Nelson::ProtobufGateway::protobufEncodeBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 4, 7);
    nargoutcheck(nLhs, 0, 1);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    std::string typeName = argIn[1].asUtf8String();
    bool delimited = argIn[3].asLogicalScalar();
    const Protobuf::Codec& codec = *entry->codec;
    std::string encoded;
    try {
        int32_t messageIndex = entry->schema.resolveMessage(typeName);
        Protobuf::ColumnSet set = codec.createColumnSet(messageIndex);
        if (argIn.size() > 4 && argIn[4].asLogicalScalar()) {
            appendColumns(
                codec, set, argIn[2], argIn.size() > 5 ? argIn[5] : ArrayOf::emptyStruct());
        } else {
            appendStructRows(codec, set, argIn[2]);
        }
        if (delimited) {
            const size_t workers = argIn.size() > 6 ? (size_t)argIn[6].asDoubleScalar() : 0;
            encoded = Protobuf::encodeDelimitedParallel(codec, set, workers);
        } else {
            if (set.rows != 1) {
                Error(L"protobuf:notScalar:singleMessageNeedsScalarStruct",
                    L"A single message needs a scalar struct; use 'Delimited' for several "
                    L"records.");
            }
            encoded = codec.encodeRow(set, 0);
        }
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:encodeError");
    }
    void* data
        = ArrayOf::allocateArrayOf(NLS_UINT8, (indexType)encoded.size(), stringVector(), false);
    if (!encoded.empty()) {
        std::memcpy(data, encoded.data(), encoded.size());
    }
    ArrayOfVector retval;
    retval.push_back(ArrayOf(NLS_UINT8, Dimensions(1, (indexType)encoded.size()), data));
    return retval;
}
//=============================================================================
