//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecodeFileBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufDecode.hpp"
#include "ProtobufFileIdentity.hpp"
#include "ProtoMappedFile.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// [value, presence, report] = __protobuf_decode_file__(id, type, path, options[, indexPath]):
// the file is mapped in memory instead of being read; a valid record index (same size and
// modification time as the file) is used to find the first record of 'Records'.
ArrayOfVector
Nelson::ProtobufGateway::protobufDecodeFileBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 4, 5);
    nargoutcheck(nLhs, 0, 3);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    const std::string typeName = argIn[1].asUtf8String();
    const std::string path = argIn[2].asUtf8String();
    const DecodeSettings settings = decodeSettings(argIn[3]);
    std::unique_ptr<Protobuf::MappedFile> file;
    try {
        file = std::make_unique<Protobuf::MappedFile>(path);
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:fileNotFound");
    }
    Protobuf::RecordIndex index;
    const bool indexed = argIn.size() > 4 && !argIn[4].isEmpty()
        && readValidIndex(argIn[4].asUtf8String(), path, file->size(), index);
    return decodeData(
        nLhs, *entry, typeName, file->data(), file->size(), settings, indexed ? &index : nullptr);
}
//=============================================================================
