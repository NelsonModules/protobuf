//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDescriptorBuiltin.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtoDescriptor.hpp"
#include "ProtobufRegistry.hpp"
#include <cstring>
//=============================================================================
using namespace Nelson;
//=============================================================================
// bytes = __protobuf_descriptor__(id): serialized FileDescriptorSet of a loaded schema.
ArrayOfVector
Nelson::ProtobufGateway::protobufDescriptorBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 1, 1);
    nargoutcheck(nLhs, 0, 1);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    const std::string bytes = Protobuf::encodeDescriptorSet(entry->schema);
    void* data
        = ArrayOf::allocateArrayOf(NLS_UINT8, (indexType)bytes.size(), stringVector(), false);
    if (!bytes.empty()) {
        std::memcpy(data, bytes.data(), bytes.size());
    }
    ArrayOfVector retval;
    retval.push_back(ArrayOf(NLS_UINT8, Dimensions(1, (indexType)bytes.size()), data));
    return retval;
}
//=============================================================================
