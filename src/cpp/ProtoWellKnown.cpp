//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoWellKnown.hpp"
#include <cstring>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    struct BuiltInFile
    {
        const char* name;
        const char* text;
    };
    //=============================================================================
    // Wire-compatible definitions: messages, field numbers and types of the well-known types.
    const BuiltInFile builtInFiles[] = {
        { "google/protobuf/timestamp.proto",
            "syntax = \"proto3\";\npackage google.protobuf;\n"
            "message Timestamp {\n  int64 seconds = 1;\n  int32 nanos = 2;\n}\n" },
        { "google/protobuf/duration.proto",
            "syntax = \"proto3\";\npackage google.protobuf;\n"
            "message Duration {\n  int64 seconds = 1;\n  int32 nanos = 2;\n}\n" },
        { "google/protobuf/wrappers.proto",
            "syntax = \"proto3\";\npackage google.protobuf;\n"
            "message DoubleValue {\n  double value = 1;\n}\n"
            "message FloatValue {\n  float value = 1;\n}\n"
            "message Int64Value {\n  int64 value = 1;\n}\n"
            "message UInt64Value {\n  uint64 value = 1;\n}\n"
            "message Int32Value {\n  int32 value = 1;\n}\n"
            "message UInt32Value {\n  uint32 value = 1;\n}\n"
            "message BoolValue {\n  bool value = 1;\n}\n"
            "message StringValue {\n  string value = 1;\n}\n"
            "message BytesValue {\n  bytes value = 1;\n}\n" },
        { "google/protobuf/empty.proto",
            "syntax = \"proto3\";\npackage google.protobuf;\nmessage Empty {\n}\n" },
        { "google/protobuf/field_mask.proto",
            "syntax = \"proto3\";\npackage google.protobuf;\n"
            "message FieldMask {\n  repeated string paths = 1;\n}\n" },
        { "google/protobuf/any.proto",
            "syntax = \"proto3\";\npackage google.protobuf;\n"
            "message Any {\n  string type_url = 1;\n  bytes value = 2;\n}\n" },
        { "google/protobuf/struct.proto",
            "syntax = \"proto3\";\npackage google.protobuf;\n"
            "message Struct {\n  map<string, Value> fields = 1;\n}\n"
            "message Value {\n  oneof kind {\n    NullValue null_value = 1;\n"
            "    double number_value = 2;\n    string string_value = 3;\n    bool bool_value = 4;\n"
            "    Struct struct_value = 5;\n    ListValue list_value = 6;\n  }\n}\n"
            "enum NullValue {\n  NULL_VALUE = 0;\n}\n"
            "message ListValue {\n  repeated Value values = 1;\n}\n" },
    };
    //=============================================================================
} // namespace
//=============================================================================
const char*
wellKnownProtoText(const std::string& name)
{
    for (const BuiltInFile& file : builtInFiles) {
        if (name == file.name) {
            return file.text;
        }
    }
    return name == "google/protobuf/descriptor.proto" ? descriptorProtoText() : nullptr;
}
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
