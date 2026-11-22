//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufLoadBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "ErrorID.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtoDescriptor.hpp"
#include "ProtobufRegistry.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
//=============================================================================
using namespace Nelson;
//=============================================================================
// [id, info] = __protobuf_load__(source, importPaths, format[, name]): importPaths is a cell of
// folders; format is 'proto' (source is a .proto file), 'descriptor' (a serialized
// FileDescriptorSet file) or 'text' (source is .proto text, named name in messages).
ArrayOfVector
Nelson::ProtobufGateway::protobufLoadBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 1, 4);
    nargoutcheck(nLhs, 0, 2);
    if (!argIn[0].isRowVectorCharacterArray()
        && !(argIn[0].isStringArray() && argIn[0].isScalar())) {
        Error(L"protobuf:invalidFilename:fileNameTextScalar",
            L"The file name must be a text scalar.");
    }
    std::string path = argIn[0].asUtf8String();
    std::vector<std::string> importPaths;
    if (argIn.size() > 1 && !argIn[1].isEmpty()) {
        importPaths = argIn[1].asUtf8StringVector(false);
    }
    const std::string format = argIn.size() > 2 ? argIn[2].asUtf8String() : "proto";
    const bool descriptorSet = format == "descriptor";
    Protobuf::Schema schema;
    try {
        if (format == "text") {
            const std::string text = path;
            path = argIn.size() > 3 ? argIn[3].asUtf8String() : std::string("input.proto");
            schema = Protobuf::parseProtoText(text, path, importPaths);
        } else if (descriptorSet) {
            std::ifstream stream(std::filesystem::path(argIn[0].asWideString()), std::ios::binary);
            if (!stream) {
                Nelson::Error(
                    "protobuf:fileNotFound:cannotOpenQuoted", "cannot open \"%s\".", path);
            }
            std::ostringstream content;
            content << stream.rdbuf();
            schema = Protobuf::parseDescriptorSet(content.str(), path);
        } else {
            schema = Protobuf::parseProtoFile(path, importPaths);
        }
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:parseError");
    }
    const Protobuf::FileDef file = schema.files[0];
    std::vector<std::string> names = schema.messageNames();
    uint64_t id = registerSchema(std::move(schema), path);
    ArrayOfVector retval;
    retval.push_back(ArrayOf::doubleScalar((double)id));
    if (nLhs > 1) {
        stringVector fieldNames = { "File", "Package", "Syntax", "Messages", "Imports" };
        ArrayOfVector values;
        values.push_back(ArrayOf::characterArray(path));
        values.push_back(ArrayOf::characterArray(file.package));
        values.push_back(ArrayOf::characterArray(file.syntax));
        values.push_back(ArrayOf::cellArrayFromArrayOfCharacterColumnVectors(names));
        values.push_back(ArrayOf::cellArrayFromArrayOfCharacterColumnVectors(file.dependencies));
        retval.push_back(ArrayOf::structScalar(fieldNames, values));
    }
    return retval;
}
//=============================================================================
