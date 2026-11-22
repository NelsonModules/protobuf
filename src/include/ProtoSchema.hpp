//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoError.hpp"
#include <cstdint>
#include <stdexcept>
#include <map>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Field types, numbered like the reference descriptor.
enum class FieldType : int
{
    Double = 1,
    Float = 2,
    Int64 = 3,
    UInt64 = 4,
    Int32 = 5,
    Fixed64 = 6,
    Fixed32 = 7,
    Bool = 8,
    String = 9,
    Group = 10,
    Message = 11,
    Bytes = 12,
    UInt32 = 13,
    Enum = 14,
    SFixed32 = 15,
    SFixed64 = 16,
    SInt32 = 17,
    SInt64 = 18
};
//=============================================================================
enum class FieldLabel : int
{
    Optional = 1,
    Required = 2,
    Repeated = 3
};
//=============================================================================
// In-memory storage class of a decoded value.
enum class ValueKind : uint8_t
{
    Double,
    Float,
    Int64,
    UInt64,
    Int32,
    UInt32,
    Bool,
    Bytes,
    Message
};
//=============================================================================
// Edition features as written on one element, numbered like the reference FeatureSet
// (0 when not set). Resolved values follow file, message, oneof and field nesting.
struct FeatureSet
{
    int8_t fieldPresence = 0; // 1 explicit, 2 implicit, 3 legacy required
    int8_t enumType = 0; // 1 open, 2 closed
    int8_t repeatedFieldEncoding = 0; // 1 packed, 2 expanded
    int8_t utf8Validation = 0; // 2 verify, 3 none
    int8_t messageEncoding = 0; // 1 length prefixed, 2 delimited
    int8_t jsonFormat = 0; // 1 allow, 2 legacy best effort
    int8_t enforceNamingStyle = 0; // 1 style 2024, 2 legacy
    int8_t defaultSymbolVisibility = 0; // 1 export all, 2 export top level, 3 local all, 4 strict
    bool
    empty() const
    {
        return fieldPresence == 0 && enumType == 0 && repeatedFieldEncoding == 0
            && utf8Validation == 0 && messageEncoding == 0 && jsonFormat == 0
            && enforceNamingStyle == 0 && defaultSymbolVisibility == 0;
    }
    // Values set here replace the ones of base.
    FeatureSet
    over(const FeatureSet& base) const;
};
//=============================================================================
// Symbol visibility written before a message or an enum (edition 2024).
enum class Visibility : int8_t
{
    Unset = 0,
    Local = 1,
    Export = 2
};
//=============================================================================
struct FieldDef
{
    std::string name;
    std::string jsonName;
    int32_t number = 0;
    FieldLabel label = FieldLabel::Optional;
    FieldType type = FieldType::Int32;
    // Type name as written, then fully qualified with a leading dot.
    std::string typeName;
    int32_t messageIndex = -1;
    int32_t enumIndex = -1;
    int32_t oneofIndex = -1;
    bool proto3Optional = false;
    bool hasPackedOption = false;
    bool packedOption = false;
    bool hasJsonNameOption = false;
    // proto2 group: the field of a nested message written inline, encoded as a group.
    bool isGroup = false;
    // Other options as written in the .proto text: name ("deprecated", "(units)") and value.
    std::vector<std::pair<std::string, std::string>> options;
    // Value of each of these options as a text format literal ("mbar" quoted, aggregates as is).
    std::vector<std::string> optionLiterals;
    // Extension: the extended message (as written, then ".full.Name") and, in the fields of
    // that message, the full name of the extension ("pkg.units"); empty for a plain field.
    std::string extendee;
    std::string extension;
    FeatureSet features;
    // Explicit default value (editions), canonical text like the reference descriptor.
    bool hasDefault = false;
    std::string defaultValue;
    // Effective properties, set once the schema is resolved.
    bool packed = false;
    bool hasPresence = false;
    bool required = false; // legacy required presence
    bool delimited = false; // message encoded as a group
    bool utf8Check = true; // string checked as UTF-8
    bool isMap = false;
    ValueKind kind = ValueKind::Int32;
    int line = 0;
    int column = 0;
};
//=============================================================================
struct OneofDef
{
    std::string name;
    bool synthetic = false;
    std::vector<int32_t> fields;
    FeatureSet features;
};
//=============================================================================
struct EnumValueDef
{
    std::string name;
    int32_t number = 0;
};
//=============================================================================
// Message ranges use an exclusive end, enum ranges an inclusive end.
struct ReservedRange
{
    int32_t start = 0;
    int32_t end = 0;
};
//=============================================================================
struct EnumDef
{
    int32_t file = 0; // index in Schema::files
    std::string name;
    std::string fullName;
    std::vector<EnumValueDef> values;
    std::vector<ReservedRange> reservedRanges;
    std::vector<std::string> reservedNames;
    bool allowAlias = false;
    FeatureSet features;
    Visibility visibility = Visibility::Unset;
    // Resolved: unknown numbers are kept as unknown fields, not as values.
    bool closed = false;
    int line = 0;
    int column = 0;
};
//=============================================================================
struct MessageDef
{
    int32_t file = 0; // index in Schema::files
    std::string name;
    std::string fullName;
    std::vector<FieldDef> fields;
    std::vector<OneofDef> oneofs;
    std::vector<int32_t> nestedMessages;
    std::vector<int32_t> nestedEnums;
    std::vector<ReservedRange> reservedRanges;
    std::vector<std::string> reservedNames;
    // Numbers left to extensions (exclusive end), and extensions declared inside the message.
    std::vector<ReservedRange> extensionRanges;
    std::vector<int32_t> extensions;
    bool mapEntry = false;
    // deprecated_legacy_json_field_conflicts: JSON name conflicts are allowed.
    bool legacyJsonConflicts = false;
    FeatureSet features;
    Visibility visibility = Visibility::Unset;
    // Features in effect for the fields and nested types (set at resolution).
    FeatureSet resolved;
    int line = 0;
    int column = 0;
    // Field number to field index tables, built at resolution.
    std::vector<int32_t> denseLookup;
    std::unordered_map<int32_t, int32_t> sparseLookup;
    int32_t
    findField(int32_t number) const;
    int32_t
    findFieldByName(const std::string& fieldName) const;
};
//=============================================================================
// Extension declared by "extend Type { ... }": the field as written (resolved like a field);
// a copy is appended to the fields of the extended message.
struct ExtensionDef
{
    int32_t file = 0; // index in Schema::files
    int32_t scope = -1; // declaring message, -1 at file level
    std::string fullName;
    FieldDef field;
};
//=============================================================================
struct MethodDef
{
    std::string name;
    std::string inputType;
    std::string outputType;
    bool clientStreaming = false;
    bool serverStreaming = false;
};
//=============================================================================
struct ServiceDef
{
    int32_t file = 0; // index in Schema::files
    std::string name;
    std::string fullName;
    std::vector<MethodDef> methods;
};
//=============================================================================
struct FileDef
{
    // Name as imported ("google/protobuf/timestamp.proto"); the base name for the loaded file.
    std::string name;
    // Path used in error messages.
    std::string path;
    // Imported names, in order; public imports are also listed in publicDependencies.
    std::vector<std::string> dependencies;
    std::vector<std::string> publicDependencies;
    std::string package;
    // "proto2", "proto3", or "editions" with edition "2023" or "2024".
    std::string syntax;
    std::string edition;
    FeatureSet features;
    // Files named by "import option" (edition 2024): custom option definitions, not loaded.
    std::vector<std::string> optionDependencies;
    std::vector<int32_t> messages;
    std::vector<int32_t> enums;
    std::vector<int32_t> services;
    std::vector<int32_t> extensions;
    std::vector<std::pair<std::string, std::string>> options;
};
//=============================================================================
class Schema
{
public:
    std::vector<FileDef> files;
    std::vector<MessageDef> messages;
    std::vector<EnumDef> enums;
    std::vector<ServiceDef> services;
    std::vector<ExtensionDef> extensions;
    std::unordered_map<std::string, int32_t> messageByName;
    std::unordered_map<std::string, int32_t> enumByName;
    // Full name with or without a leading dot; -1 when absent.
    int32_t
    findMessage(const std::string& fullName) const;
    int32_t
    findEnum(const std::string& fullName) const;
    // Top-level and nested message names, map entries excluded.
    std::vector<std::string>
    messageNames() const;
    // Full name, or a unique trailing part such as "Record" or "Outer.Record"; throws otherwise.
    int32_t
    resolveMessage(const std::string& name) const;
};
//=============================================================================
// Parse and resolve one .proto source held in memory; its imports are searched in
// importPaths, then among the built-in well-known types.
Schema
parseProtoText(const std::string& text, const std::string& fileName,
    const std::vector<std::string>& importPaths = { });
//=============================================================================
// The first file of Schema::files is the loaded one, its imports follow. Imports are searched
// in the folder of the importing file, then in importPaths, then among the well-known types
// built in (google/protobuf/timestamp.proto...).
Schema
parseProtoFile(const std::string& path, const std::vector<std::string>& importPaths = { });
//=============================================================================
// Schema of in-memory .proto files: root is a key of files; imports are looked up in files,
// then among the built-in well-known types. checkNaming false skips the edition 2024 naming
// style (descriptor sets do not keep enforce_naming_style, a source-only feature).
Schema
parseProtoTexts(const std::map<std::string, std::string>& files, const std::string& root,
    bool checkNaming = true);
//=============================================================================
ValueKind
valueKindOf(FieldType type);
//=============================================================================
bool
isPackableType(FieldType type);
//=============================================================================
// Keyword of a scalar type in .proto files ("int32", "string"...); "message", "enum", "group".
std::string
fieldTypeName(FieldType type);
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
