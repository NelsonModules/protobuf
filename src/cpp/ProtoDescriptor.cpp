//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoDescriptor.hpp"
#include "ProtoWire.hpp"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    // Fields of descriptor.proto read by the loader (field numbers in comments).
    struct DescField
    {
        std::string name; // 1
        std::string extendee; // 2, extensions only
        int32_t number = 0; // 3
        int32_t label = 1; // 4
        int32_t type = 0; // 5
        std::string typeName; // 6
        std::string jsonName; // 10
        bool hasJsonName = false;
        int32_t oneofIndex = -1; // 9
        bool proto3Optional = false; // 17
        bool hasPacked = false; // options 8 / packed 2
        bool packed = false;
        bool hasDefault = false; // 7
        std::string defaultValue;
        FeatureSet features; // options 8 / features 50
    };
    struct DescRange
    {
        int32_t start = 0;
        int32_t end = 0;
    };
    struct DescEnum
    {
        std::string name;
        std::vector<std::pair<std::string, int32_t>> values;
        std::vector<DescRange> reservedRanges; // end inclusive
        std::vector<std::string> reservedNames;
        bool allowAlias = false;
        FeatureSet features;
        int32_t visibility = 0; // 6
    };
    struct DescMessage
    {
        std::string name;
        std::vector<DescField> fields;
        std::vector<DescMessage> nested;
        std::vector<DescEnum> enums;
        std::vector<std::string> oneofs;
        std::vector<FeatureSet> oneofFeatures;
        std::vector<DescRange> reservedRanges; // end exclusive
        std::vector<std::string> reservedNames;
        std::vector<DescRange> extensionRanges; // 5, end exclusive
        std::vector<DescField> extensions; // 6
        bool mapEntry = false;
        bool legacyJsonConflicts = false; // options 11
        FeatureSet features;
        int32_t visibility = 0; // 11
    };
    struct DescMethod
    {
        std::string name;
        std::string input;
        std::string output;
        bool clientStreaming = false;
        bool serverStreaming = false;
    };
    struct DescService
    {
        std::string name;
        std::vector<DescMethod> methods;
    };
    struct DescFile
    {
        std::string name;
        std::string package;
        std::vector<std::string> dependencies;
        std::vector<int32_t> publicDependencies;
        std::vector<DescMessage> messages;
        std::vector<DescEnum> enums;
        std::vector<DescService> services;
        std::vector<DescField> extensions; // 7
        std::string syntax;
        std::string edition; // 14, as "2023" or "2024"
        std::vector<std::string> optionDependencies; // 15
        FeatureSet features;
    };
    //=============================================================================
    // Reads the fields of one message: handler(number, wireType, p, end) returns false to skip.
    template <typename Handler>
    void
    readMessage(const uint8_t* p, const uint8_t* end, Handler handler);
    std::string
    readString(const uint8_t*& p, const uint8_t* end);
    DescFile
    readFile(const uint8_t* p, const uint8_t* end);
    DescMessage
    readDescMessage(const uint8_t* p, const uint8_t* end);
    DescEnum
    readDescEnum(const uint8_t* p, const uint8_t* end);
    std::string
    fileText(const DescFile& file);
    void
    messageText(std::string& out, const DescMessage& message, const std::string& pad, bool editions,
        bool proto2, const std::string& header);
    void
    enumText(std::string& out, const DescEnum& e, const std::string& pad, bool editions);
    // "export " or "local " for a visibility written in the descriptor.
    std::string
    visibilityText(int32_t visibility);
    std::string
    quoted(const std::string& text);
    std::string
    defaultJsonName(const std::string& name);
    // Writers of the descriptor wire format.
    void
    putVarint(std::string& out, uint64_t value);
    void
    putTag(std::string& out, int32_t number, uint32_t wireType);
    void
    putString(std::string& out, int32_t number, const std::string& value);
    void
    putInt(std::string& out, int32_t number, int64_t value);
    void
    putMessage(std::string& out, int32_t number, const std::string& body);
    std::string
    messageDescriptor(const Schema& schema, int32_t messageIndex);
    // FieldDescriptorProto of a field or an extension (extendee set).
    std::string
    fieldDescriptor(const FieldDef& f);
    // Extensions as "extend" blocks; a group is written with its message, taken from nested.
    void
    extendText(std::string& out, const std::vector<DescField>& extensions,
        const std::vector<DescMessage>& nested, const std::string& pad, bool editions, bool proto2);
    // Index in nested of the message of an extension group, -1 otherwise.
    int64_t
    extensionGroup(const DescField& f, const std::vector<DescMessage>& nested);
    std::string
    enumDescriptor(const EnumDef& e);
    std::string
    fileDescriptor(const Schema& schema, const FileDef& file);
    // FeatureSet message (options field 50), read and written.
    FeatureSet
    readFeatures(const uint8_t* p, const uint8_t* end);
    std::string
    featuresDescriptor(const FeatureSet& features);
    // Options message (field number of out) holding the features only when some are set.
    void
    putFeatureOptions(
        std::string& out, int32_t number, int32_t featuresNumber, const FeatureSet& features);
    // "features.name = VALUE" texts of the features set.
    std::vector<std::string>
    featureTexts(const FeatureSet& features);
    // Reads an options message whose field featuresNumber holds the features (50 in FileOptions,
    // 12 in MessageOptions, 21 in FieldOptions, 1 in OneofOptions, 7 in EnumOptions); other
    // handled fields go to other.
    template <typename Other>
    void
    readOptions(const uint8_t*& q, const uint8_t* e, int32_t featuresNumber, FeatureSet& features,
        Other other);
    // String default value written as a .proto literal.
    std::string
    escapedLiteral(const std::string& text);
    // Name and imports of a file descriptor, without reading its definitions.
    void
    fileHeader(const std::string& bytes, std::string& name, std::vector<std::string>& dependencies);
    //=============================================================================
} // namespace
//=============================================================================
Schema
parseDescriptorSet(const std::string& bytes, const std::string& setName)
{
    std::map<std::string, std::string> files;
    std::string root;
    try {
        std::map<std::string, std::string> raw;
        std::map<std::string, std::vector<std::string>> imports;
        const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
        readMessage(p, p + bytes.size(),
            [&](int32_t number, uint32_t wire, const uint8_t*& q, const uint8_t* end) {
                if (number != 1 || wire != Wire::kLength) {
                    return false;
                }
                const size_t length = Wire::readLength(q, end);
                std::string body(reinterpret_cast<const char*>(q), length);
                q += length;
                std::string name;
                std::vector<std::string> deps;
                fileHeader(body, name, deps);
                imports[name] = deps;
                raw[name] = body;
                root = name;
                return true;
            });
        // Files used only for custom options (edition 2024 "import option") are left out.
        std::vector<std::string> todo { root };
        while (!todo.empty()) {
            const std::string name = todo.back();
            todo.pop_back();
            auto it = raw.find(name);
            if (it == raw.end() || files.count(name) != 0) {
                continue;
            }
            std::string unused;
            files[name] = descriptorToProtoText(it->second, unused);
            for (const std::string& dependency : imports[name]) {
                todo.push_back(dependency);
            }
        }
    } catch (const ProtoError& e) {
        Error("protobuf:parseError:validDescriptorSet", "%s: not a valid descriptor set (%s)",
            setName, e);
    }
    if (root.empty()) {
        Error("protobuf:parseError:descriptorSetFile", "%s: the descriptor set has no file.",
            setName);
    }
    return parseProtoTexts(files, root, false);
}
//=============================================================================
std::string
descriptorToProtoText(const std::string& fileDescriptorProto, std::string& fileName)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(fileDescriptorProto.data());
    DescFile file = readFile(p, p + fileDescriptorProto.size());
    fileName = file.name;
    return fileText(file);
}
//=============================================================================
std::string
encodeDescriptorSet(const Schema& schema)
{
    // Post-order from the loaded file: a file comes after the files it imports.
    std::map<std::string, size_t> byName;
    for (size_t k = 0; k < schema.files.size(); ++k) {
        byName[schema.files[k].name] = k;
    }
    std::vector<size_t> order;
    std::set<size_t> seen;
    std::vector<std::pair<size_t, size_t>> stack { { 0, 0 } };
    seen.insert(0);
    while (!stack.empty()) {
        auto& [file, next] = stack.back();
        const std::vector<std::string>& deps = schema.files[file].dependencies;
        if (next < deps.size()) {
            auto it = byName.find(deps[next++]);
            if (it != byName.end() && seen.insert(it->second).second) {
                stack.push_back({ it->second, 0 });
            }
            continue;
        }
        order.push_back(file);
        stack.pop_back();
    }
    std::string out;
    for (size_t index : order) {
        putMessage(out, 1, fileDescriptor(schema, schema.files[index]));
    }
    return out;
}
//=============================================================================
namespace {
    //=============================================================================
    void
    skip(uint32_t wire, const uint8_t*& p, const uint8_t* end)
    {
        switch (wire) {
        case Wire::kVarint:
            Wire::readVarint(p, end);
            break;
        case Wire::kFixed64:
            Wire::readFixed64(p, end);
            break;
        case Wire::kLength:
            p += Wire::readLength(p, end);
            break;
        case Wire::kFixed32:
            Wire::readFixed32(p, end);
            break;
        default:
            Error("protobuf:parseError:unsupportedWireTypeDescriptor",
                "unsupported wire type in a descriptor.");
        }
    }
    //=============================================================================
    template <typename Handler>
    void
    readMessage(const uint8_t* p, const uint8_t* end, Handler handler)
    {
        while (p < end) {
            const uint64_t tag = Wire::readVarint(p, end);
            const uint32_t wire = (uint32_t)(tag & 7);
            if (!handler((int32_t)(tag >> 3), wire, p, end)) {
                skip(wire, p, end);
            }
        }
    }
    //=============================================================================
    std::string
    readString(const uint8_t*& p, const uint8_t* end)
    {
        const size_t length = Wire::readLength(p, end);
        std::string text(reinterpret_cast<const char*>(p), length);
        p += length;
        return text;
    }
    //=============================================================================
    // Calls each(p, end) on the bytes of a length-delimited field.
    template <typename Each>
    void
    readSub(const uint8_t*& p, const uint8_t* end, Each each)
    {
        const size_t length = Wire::readLength(p, end);
        each(p, p + length);
        p += length;
    }
    //=============================================================================
    DescRange
    readRange(const uint8_t* p, const uint8_t* end)
    {
        DescRange range;
        readMessage(p, end, [&](int32_t n, uint32_t w, const uint8_t*& q, const uint8_t* e) {
            if (w != Wire::kVarint || (n != 1 && n != 2)) {
                return false;
            }
            (n == 1 ? range.start : range.end) = (int32_t)Wire::readVarint(q, e);
            return true;
        });
        return range;
    }
    //=============================================================================
    DescField
    readDescField(const uint8_t* p, const uint8_t* end)
    {
        DescField field;
        readMessage(p, end, [&](int32_t n, uint32_t w, const uint8_t*& q, const uint8_t* e) {
            switch (n) {
            case 1:
                field.name = readString(q, e);
                return true;
            case 2:
                field.extendee = readString(q, e);
                return true;
            case 3:
                field.number = (int32_t)Wire::readVarint(q, e);
                return true;
            case 4:
                field.label = (int32_t)Wire::readVarint(q, e);
                return true;
            case 5:
                field.type = (int32_t)Wire::readVarint(q, e);
                return true;
            case 6:
                field.typeName = readString(q, e);
                return true;
            case 9:
                field.oneofIndex = (int32_t)Wire::readVarint(q, e);
                return true;
            case 10:
                field.jsonName = readString(q, e);
                field.hasJsonName = true;
                return true;
            case 17:
                field.proto3Optional = Wire::readVarint(q, e) != 0;
                return true;
            case 7:
                field.hasDefault = true;
                field.defaultValue = readString(q, e);
                return true;
            case 8:
                readOptions(q, e, 21, field.features,
                    [&](int32_t on, uint32_t ow, const uint8_t*& r, const uint8_t* s) {
                        if (on != 2 || ow != Wire::kVarint) {
                            return false;
                        }
                        field.hasPacked = true;
                        field.packed = Wire::readVarint(r, s) != 0;
                        return true;
                    });
                return true;
            default:
                (void)w;
                return false;
            }
        });
        return field;
    }
    //=============================================================================
    template <typename Other>
    void
    readOptions(const uint8_t*& q, const uint8_t* e, int32_t featuresNumber, FeatureSet& features,
        Other other)
    {
        readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
            readMessage(a, b, [&](int32_t on, uint32_t ow, const uint8_t*& r, const uint8_t* s) {
                if (on == featuresNumber && ow == Wire::kLength) {
                    const size_t length = Wire::readLength(r, s);
                    features = readFeatures(r, r + length);
                    r += length;
                    return true;
                }
                return other(on, ow, r, s);
            });
        });
    }
    //=============================================================================
    FeatureSet
    readFeatures(const uint8_t* p, const uint8_t* end)
    {
        FeatureSet features;
        int8_t FeatureSet::* members[] = { &FeatureSet::fieldPresence, &FeatureSet::enumType,
            &FeatureSet::repeatedFieldEncoding, &FeatureSet::utf8Validation,
            &FeatureSet::messageEncoding, &FeatureSet::jsonFormat, &FeatureSet::enforceNamingStyle,
            &FeatureSet::defaultSymbolVisibility };
        readMessage(p, end, [&](int32_t n, uint32_t w, const uint8_t*& q, const uint8_t* e) {
            if (w != Wire::kVarint || n < 1 || n > 8) {
                return false;
            }
            features.*members[n - 1] = (int8_t)Wire::readVarint(q, e);
            return true;
        });
        return features;
    }
    //=============================================================================
    void
    fileHeader(const std::string& bytes, std::string& name, std::vector<std::string>& dependencies)
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes.data());
        readMessage(
            p, p + bytes.size(), [&](int32_t n, uint32_t w, const uint8_t*& q, const uint8_t* e) {
                if (w != Wire::kLength || (n != 1 && n != 3)) {
                    return false;
                }
                (n == 1 ? name : dependencies.emplace_back()) = readString(q, e);
                return true;
            });
    }
    //=============================================================================
    DescEnum
    readDescEnum(const uint8_t* p, const uint8_t* end)
    {
        DescEnum result;
        readMessage(p, end, [&](int32_t n, uint32_t w, const uint8_t*& q, const uint8_t* e) {
            if (n == 6 && w == Wire::kVarint) {
                result.visibility = (int32_t)Wire::readVarint(q, e);
                return true;
            }
            if (w != Wire::kLength) {
                return false;
            }
            switch (n) {
            case 1:
                result.name = readString(q, e);
                return true;
            case 2:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    std::pair<std::string, int32_t> value;
                    readMessage(
                        a, b, [&](int32_t vn, uint32_t vw, const uint8_t*& r, const uint8_t* s) {
                            if (vn == 1 && vw == Wire::kLength) {
                                value.first = readString(r, s);
                                return true;
                            }
                            if (vn == 2 && vw == Wire::kVarint) {
                                value.second = (int32_t)Wire::readVarint(r, s);
                                return true;
                            }
                            return false;
                        });
                    result.values.push_back(value);
                });
                return true;
            case 3:
                readOptions(q, e, 7, result.features,
                    [&](int32_t on, uint32_t ow, const uint8_t*& r, const uint8_t* s) {
                        if (on != 2 || ow != Wire::kVarint) {
                            return false;
                        }
                        result.allowAlias = Wire::readVarint(r, s) != 0;
                        return true;
                    });
                return true;
            case 4:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    result.reservedRanges.push_back(readRange(a, b));
                });
                return true;
            case 5:
                result.reservedNames.push_back(readString(q, e));
                return true;
            default:
                return false;
            }
        });
        return result;
    }
    //=============================================================================
    DescMessage
    readDescMessage(const uint8_t* p, const uint8_t* end)
    {
        DescMessage result;
        readMessage(p, end, [&](int32_t n, uint32_t w, const uint8_t*& q, const uint8_t* e) {
            if (n == 11 && w == Wire::kVarint) {
                result.visibility = (int32_t)Wire::readVarint(q, e);
                return true;
            }
            if (w != Wire::kLength) {
                return false;
            }
            switch (n) {
            case 1:
                result.name = readString(q, e);
                return true;
            case 2:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    result.fields.push_back(readDescField(a, b));
                });
                return true;
            case 3:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    result.nested.push_back(readDescMessage(a, b));
                });
                return true;
            case 4:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    result.enums.push_back(readDescEnum(a, b));
                });
                return true;
            case 6:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    result.extensions.push_back(readDescField(a, b));
                });
                return true;
            case 5:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    result.extensionRanges.push_back(readRange(a, b));
                });
                return true;
            case 7:
                readOptions(q, e, 12, result.features,
                    [&](int32_t on, uint32_t ow, const uint8_t*& r, const uint8_t* s) {
                        if (ow != Wire::kVarint || (on != 1 && on != 7 && on != 11)) {
                            return false;
                        }
                        const bool set = Wire::readVarint(r, s) != 0;
                        if (on == 1 && set) {
                            Error("protobuf:parseError:messageSetNotSupported",
                                "message_set_wire_format is not supported.");
                        }
                        (on == 7 ? result.mapEntry : result.legacyJsonConflicts) |= on != 1 && set;
                        return true;
                    });
                return true;
            case 8:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    std::string name;
                    FeatureSet features;
                    readMessage(
                        a, b, [&](int32_t on, uint32_t ow, const uint8_t*& r, const uint8_t* s) {
                            if (on == 2 && ow == Wire::kLength) {
                                readOptions(r, s, 1, features,
                                    [](int32_t, uint32_t, const uint8_t*&, const uint8_t*) {
                                        return false;
                                    });
                                return true;
                            }
                            if (on != 1 || ow != Wire::kLength) {
                                return false;
                            }
                            name = readString(r, s);
                            return true;
                        });
                    result.oneofs.push_back(name);
                    result.oneofFeatures.push_back(features);
                });
                return true;
            case 9:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    result.reservedRanges.push_back(readRange(a, b));
                });
                return true;
            case 10:
                result.reservedNames.push_back(readString(q, e));
                return true;
            default:
                return false;
            }
        });
        return result;
    }
    //=============================================================================
    DescFile
    readFile(const uint8_t* p, const uint8_t* end)
    {
        DescFile file;
        readMessage(p, end, [&](int32_t n, uint32_t w, const uint8_t*& q, const uint8_t* e) {
            if (n == 10 && w == Wire::kVarint) {
                file.publicDependencies.push_back((int32_t)Wire::readVarint(q, e));
                return true;
            }
            if (n == 14 && w == Wire::kVarint) {
                const uint64_t edition = Wire::readVarint(q, e);
                file.edition = edition == 1000
                    ? "2023"
                    : (edition == 1001 ? "2024"
                                       : (edition == 1002 ? "2026" : std::to_string(edition)));
                return true;
            }
            if (w != Wire::kLength) {
                return false;
            }
            switch (n) {
            case 1:
                file.name = readString(q, e);
                return true;
            case 2:
                file.package = readString(q, e);
                return true;
            case 3:
                file.dependencies.push_back(readString(q, e));
                return true;
            case 4:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    file.messages.push_back(readDescMessage(a, b));
                });
                return true;
            case 5:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    file.enums.push_back(readDescEnum(a, b));
                });
                return true;
            case 6:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    DescService service;
                    readMessage(
                        a, b, [&](int32_t sn, uint32_t sw, const uint8_t*& r, const uint8_t* s) {
                            if (sw != Wire::kLength) {
                                return false;
                            }
                            if (sn == 1) {
                                service.name = readString(r, s);
                                return true;
                            }
                            if (sn != 2) {
                                return false;
                            }
                            readSub(r, s, [&](const uint8_t* c, const uint8_t* d) {
                                DescMethod method;
                                readMessage(c, d,
                                    [&](int32_t mn, uint32_t mw, const uint8_t*& t,
                                        const uint8_t* u) {
                                        if (mw == Wire::kLength && mn >= 1 && mn <= 3) {
                                            std::string text = readString(t, u);
                                            (mn == 1 ? method.name
                                                     : (mn == 2 ? method.input : method.output))
                                                = text;
                                            return true;
                                        }
                                        if (mw == Wire::kVarint && (mn == 5 || mn == 6)) {
                                            (mn == 5 ? method.clientStreaming
                                                     : method.serverStreaming)
                                                = Wire::readVarint(t, u) != 0;
                                            return true;
                                        }
                                        return false;
                                    });
                                service.methods.push_back(method);
                            });
                            return true;
                        });
                    file.services.push_back(service);
                });
                return true;
            case 7:
                readSub(q, e, [&](const uint8_t* a, const uint8_t* b) {
                    file.extensions.push_back(readDescField(a, b));
                });
                return true;
            case 8:
                readOptions(q, e, 50, file.features,
                    [](int32_t, uint32_t, const uint8_t*&, const uint8_t*) { return false; });
                return true;
            case 12:
                file.syntax = readString(q, e);
                return true;
            case 15:
                file.optionDependencies.push_back(readString(q, e));
                return true;
            default:
                return false;
            }
        });
        return file;
    }
    //=============================================================================
    std::string
    quoted(const std::string& text)
    {
        std::string out = "\"";
        for (char c : text) {
            if (c == '"' || c == '\\') {
                out.push_back('\\');
            }
            out.push_back(c);
        }
        return out + "\"";
    }
    //=============================================================================
    std::string
    defaultJsonName(const std::string& name)
    {
        std::string out;
        bool upper = false;
        for (char c : name) {
            if (c == '_') {
                upper = true;
            } else {
                out.push_back(upper && c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c);
                upper = false;
            }
        }
        return out;
    }
    //=============================================================================
    std::string
    fieldTypeText(const DescField& field)
    {
        if (field.type == (int)FieldType::Message || field.type == (int)FieldType::Enum
            || field.type == (int)FieldType::Group) {
            return field.typeName;
        }
        return fieldTypeName((FieldType)field.type);
    }
    //=============================================================================
    std::string
    fieldOptions(const DescField& field, bool proto2)
    {
        std::vector<std::string> options;
        if (field.hasPacked) {
            options.push_back(std::string("packed = ") + (field.packed ? "true" : "false"));
        }
        if (field.hasDefault) {
            const bool text
                = field.type == (int)FieldType::String || field.type == (int)FieldType::Bytes;
            // Bytes defaults are stored escaped already.
            options.push_back("default = "
                + (text ? "\""
                            + (field.type == (int)FieldType::Bytes
                                    ? field.defaultValue
                                    : escapedLiteral(field.defaultValue))
                            + "\""
                        : field.defaultValue));
        }
        FeatureSet features = field.features;
        // In an editions set, a required label and a group are features (proto2 writes them).
        if (!proto2 && field.label == 2 && features.fieldPresence == 0) {
            features.fieldPresence = 3;
        }
        if (!proto2 && field.type == (int)FieldType::Group && features.messageEncoding == 0) {
            features.messageEncoding = 2;
        }
        for (const std::string& feature : featureTexts(features)) {
            options.push_back(feature);
        }
        if (field.hasJsonName && field.jsonName != defaultJsonName(field.name)) {
            options.push_back("json_name = " + quoted(field.jsonName));
        }
        if (options.empty()) {
            return "";
        }
        std::string out = " [";
        for (size_t k = 0; k < options.size(); ++k) {
            out += (k ? ", " : "") + options[k];
        }
        return out + "]";
    }
    //=============================================================================
    void
    rangesText(std::string& out, const std::vector<DescRange>& ranges, bool endExclusive,
        const std::vector<std::string>& names, const std::string& pad, bool editions)
    {
        if (!ranges.empty()) {
            out += pad + "reserved ";
            for (size_t k = 0; k < ranges.size(); ++k) {
                const int32_t last = endExclusive ? ranges[k].end - 1 : ranges[k].end;
                out += (k ? ", " : "") + std::to_string(ranges[k].start);
                if (last == 536870911 && endExclusive) {
                    out += " to max";
                } else if (last != ranges[k].start) {
                    out += " to " + std::to_string(last);
                }
            }
            out += ";\n";
        }
        if (!names.empty()) {
            out += pad + "reserved ";
            for (size_t k = 0; k < names.size(); ++k) {
                out += (k ? ", " : "") + (editions ? names[k] : quoted(names[k]));
            }
            out += ";\n";
        }
    }
    //=============================================================================
    void
    enumText(std::string& out, const DescEnum& e, const std::string& pad, bool editions)
    {
        out += pad + visibilityText(e.visibility) + "enum " + e.name + " {\n";
        if (e.allowAlias) {
            out += pad + "  option allow_alias = true;\n";
        }
        for (const std::string& feature : featureTexts(e.features)) {
            out += pad + "  option " + feature + ";\n";
        }
        for (const auto& value : e.values) {
            out += pad + "  " + value.first + " = " + std::to_string(value.second) + ";\n";
        }
        rangesText(out, e.reservedRanges, false, e.reservedNames, pad + "  ", editions);
        out += pad + "}\n";
    }
    //=============================================================================
    void
    messageText(std::string& out, const DescMessage& message, const std::string& pad, bool editions,
        bool proto2, const std::string& header)
    {
        const std::string inner = pad + "  ";
        out += header.empty()
            ? pad + visibilityText(message.visibility) + "message " + message.name + " {\n"
            : header;
        for (const std::string& feature : featureTexts(message.features)) {
            out += inner + "option " + feature + ";\n";
        }
        if (message.legacyJsonConflicts) {
            out += inner + "option deprecated_legacy_json_field_conflicts = true;\n";
        }
        // Real oneofs: the ones with a field that is not proto3 optional.
        std::vector<bool> real(message.oneofs.size(), false);
        for (const DescField& f : message.fields) {
            if (f.oneofIndex >= 0 && !f.proto3Optional) {
                real[(size_t)f.oneofIndex] = true;
            }
        }
        std::vector<bool> written(message.oneofs.size(), false);
        // Nested messages keep their order: the entry message of a map field takes the place of the
        // field, so the nested messages listed before it are written first.
        std::vector<bool> nestedDone(message.nested.size(), false);
        // proto2 groups: their message is written with the field.
        auto groupOf = [&](const DescField& f) -> int64_t {
            if (!proto2 || f.type != (int)FieldType::Group) {
                return -1;
            }
            const std::string name = f.typeName.substr(f.typeName.find_last_of('.') + 1);
            for (size_t j = 0; j < message.nested.size(); ++j) {
                if (message.nested[j].name == name) {
                    return (int64_t)j;
                }
            }
            return -1;
        };
        for (const DescField& f : message.fields) {
            const int64_t j = groupOf(f);
            if (j >= 0) {
                nestedDone[(size_t)j] = true;
            }
        }
        // Extension groups: their message is written in the extend block, which takes the
        // place of the first one.
        size_t firstGroup = message.nested.size();
        for (const DescField& f : message.extensions) {
            const int64_t j = proto2 ? extensionGroup(f, message.nested) : -1;
            if (j >= 0) {
                nestedDone[(size_t)j] = true;
                firstGroup = std::min(firstGroup, (size_t)j);
            }
        }
        auto nestedBefore = [&](size_t stop) {
            for (size_t k = 0; k < stop; ++k) {
                if (!nestedDone[k] && !message.nested[k].mapEntry) {
                    messageText(out, message.nested[k], inner, editions, proto2, "");
                    nestedDone[k] = true;
                }
            }
        };
        auto fieldLine = [&](const DescField& f, const std::string& indent) {
            // A map field points to a nested map entry message.
            for (size_t j = 0; j < message.nested.size(); ++j) {
                const DescMessage& nested = message.nested[j];
                if (nested.mapEntry && f.typeName.size() > nested.name.size()
                    && f.typeName.compare(f.typeName.size() - nested.name.size() - 1,
                           std::string::npos, "." + nested.name)
                        == 0
                    && nested.fields.size() == 2) {
                    nestedBefore(j);
                    out += indent + "map<" + fieldTypeText(nested.fields[0]) + ", "
                        + fieldTypeText(nested.fields[1]) + "> " + f.name + " = "
                        + std::to_string(f.number) + fieldOptions(f, proto2) + ";\n";
                    return;
                }
            }
            std::string label = f.proto3Optional ? "optional " : (f.label == 3 ? "repeated " : "");
            if (editions && f.label != 3) {
                label.clear();
            }
            if (proto2 && f.oneofIndex < 0) {
                label = f.label == 3 ? "repeated " : (f.label == 2 ? "required " : "optional ");
            }
            const int64_t group = groupOf(f);
            if (group >= 0) {
                // Messages listed before the group keep their place.
                for (size_t k = 0; k < (size_t)group; ++k) {
                    if (!nestedDone[k] && !message.nested[k].mapEntry) {
                        messageText(out, message.nested[k], inner, editions, proto2, "");
                        nestedDone[k] = true;
                    }
                }
                const DescMessage& nested = message.nested[(size_t)group];
                messageText(out, nested, indent, editions, proto2,
                    indent + label + "group " + nested.name + " = " + std::to_string(f.number)
                        + fieldOptions(f, proto2) + " {\n");
                return;
            }
            out += indent + label + fieldTypeText(f) + " " + f.name + " = "
                + std::to_string(f.number) + fieldOptions(f, proto2) + ";\n";
        };
        for (const DescField& f : message.fields) {
            if (f.oneofIndex >= 0 && real[(size_t)f.oneofIndex]) {
                if (written[(size_t)f.oneofIndex]) {
                    continue;
                }
                written[(size_t)f.oneofIndex] = true;
                out += inner + "oneof " + message.oneofs[(size_t)f.oneofIndex] + " {\n";
                if ((size_t)f.oneofIndex < message.oneofFeatures.size()) {
                    for (const std::string& feature :
                        featureTexts(message.oneofFeatures[(size_t)f.oneofIndex])) {
                        out += inner + "  option " + feature + ";\n";
                    }
                }
                for (const DescField& member : message.fields) {
                    if (member.oneofIndex == f.oneofIndex) {
                        fieldLine(member, inner + "  ");
                    }
                }
                out += inner + "}\n";
                continue;
            }
            fieldLine(f, inner);
        }
        nestedBefore(firstGroup);
        extendText(out, message.extensions, message.nested, inner, editions, proto2);
        nestedBefore(message.nested.size());
        for (const DescEnum& e : message.enums) {
            enumText(out, e, inner, editions);
        }
        rangesText(out, message.reservedRanges, true, message.reservedNames, inner, editions);
        if (!message.extensionRanges.empty()) {
            out += inner + "extensions ";
            for (size_t k = 0; k < message.extensionRanges.size(); ++k) {
                const DescRange& r = message.extensionRanges[k];
                out += (k ? ", " : "") + std::to_string(r.start);
                if (r.end - 1 == 536870911) {
                    out += " to max";
                } else if (r.end - 1 != r.start) {
                    out += " to " + std::to_string(r.end - 1);
                }
            }
            out += ";\n";
        }
        out += pad + "}\n";
    }
    //=============================================================================
    std::string
    fileText(const DescFile& file)
    {
        const bool editions = file.syntax == "editions";
        const bool proto2 = file.syntax.empty() || file.syntax == "proto2";
        std::string out = editions
            ? "edition = " + quoted(file.edition) + ";\n"
            : "syntax = " + quoted(file.syntax.empty() ? "proto2" : file.syntax) + ";\n";
        for (const std::string& feature : featureTexts(file.features)) {
            out += "option " + feature + ";\n";
        }
        for (const std::string& dependency : file.optionDependencies) {
            out += "import option " + quoted(dependency) + ";\n";
        }
        if (!file.package.empty()) {
            out += "package " + file.package + ";\n";
        }
        for (size_t k = 0; k < file.dependencies.size(); ++k) {
            const bool isPublic = std::find(file.publicDependencies.begin(),
                                      file.publicDependencies.end(), (int32_t)k)
                != file.publicDependencies.end();
            out += std::string("import ") + (isPublic ? "public " : "")
                + quoted(file.dependencies[k]) + ";\n";
        }
        // Extension groups: their message is written in the extend block, which takes the
        // place of the first one.
        std::vector<bool> done(file.messages.size(), false);
        size_t firstGroup = file.messages.size();
        for (const DescField& f : file.extensions) {
            const int64_t j = proto2 ? extensionGroup(f, file.messages) : -1;
            if (j >= 0) {
                done[(size_t)j] = true;
                firstGroup = std::min(firstGroup, (size_t)j);
            }
        }
        for (size_t k = 0; k <= file.messages.size(); ++k) {
            if (k == firstGroup) {
                extendText(out, file.extensions, file.messages, "", editions, proto2);
            }
            if (k < file.messages.size() && !done[k]) {
                messageText(out, file.messages[k], "", editions, proto2, "");
            }
        }
        for (const DescEnum& e : file.enums) {
            enumText(out, e, "", editions);
        }
        for (const DescService& s : file.services) {
            out += "service " + s.name + " {\n";
            for (const DescMethod& m : s.methods) {
                out += "  rpc " + m.name + " (" + (m.clientStreaming ? "stream " : "") + m.input
                    + ") returns (" + (m.serverStreaming ? "stream " : "") + m.output + ");\n";
            }
            out += "}\n";
        }
        return out;
    }
    //=============================================================================
    void
    putVarint(std::string& out, uint64_t value)
    {
        while (value >= 0x80) {
            out.push_back((char)(value | 0x80));
            value >>= 7;
        }
        out.push_back((char)value);
    }
    //=============================================================================
    void
    putTag(std::string& out, int32_t number, uint32_t wireType)
    {
        putVarint(out, ((uint64_t)number << 3) | wireType);
    }
    //=============================================================================
    void
    putString(std::string& out, int32_t number, const std::string& value)
    {
        putTag(out, number, Wire::kLength);
        putVarint(out, value.size());
        out += value;
    }
    //=============================================================================
    void
    putInt(std::string& out, int32_t number, int64_t value)
    {
        putTag(out, number, Wire::kVarint);
        putVarint(out, (uint64_t)value);
    }
    //=============================================================================
    void
    putMessage(std::string& out, int32_t number, const std::string& body)
    {
        putString(out, number, body);
    }
    //=============================================================================
    std::string
    rangeDescriptor(const ReservedRange& range)
    {
        std::string out;
        putInt(out, 1, range.start);
        putInt(out, 2, range.end);
        return out;
    }
    //=============================================================================
    std::string
    enumDescriptor(const EnumDef& e)
    {
        std::string out;
        putString(out, 1, e.name);
        for (const EnumValueDef& v : e.values) {
            std::string value;
            putString(value, 1, v.name);
            putInt(value, 2, v.number);
            putMessage(out, 2, value);
        }
        if (e.allowAlias || !e.features.empty()) {
            std::string options;
            if (e.allowAlias) {
                putInt(options, 2, 1);
            }
            if (!e.features.empty()) {
                putMessage(options, 7, featuresDescriptor(e.features));
            }
            putMessage(out, 3, options);
        }
        for (const ReservedRange& range : e.reservedRanges) {
            putMessage(out, 4, rangeDescriptor(range));
        }
        for (const std::string& name : e.reservedNames) {
            putString(out, 5, name);
        }
        if (e.visibility != Visibility::Unset) {
            putInt(out, 6, (int)e.visibility);
        }
        return out;
    }
    //=============================================================================
    std::string
    messageDescriptor(const Schema& schema, int32_t messageIndex)
    {
        const MessageDef& message = schema.messages[(size_t)messageIndex];
        std::string out;
        putString(out, 1, message.name);
        for (const FieldDef& f : message.fields) {
            if (f.extension.empty()) {
                putMessage(out, 2, fieldDescriptor(f));
            }
        }
        for (int32_t nested : message.nestedMessages) {
            putMessage(out, 3, messageDescriptor(schema, nested));
        }
        for (int32_t nested : message.nestedEnums) {
            putMessage(out, 4, enumDescriptor(schema.enums[(size_t)nested]));
        }
        for (const ReservedRange& range : message.extensionRanges) {
            putMessage(out, 5, rangeDescriptor(range));
        }
        for (int32_t x : message.extensions) {
            putMessage(out, 6, fieldDescriptor(schema.extensions[(size_t)x].field));
        }
        if (message.mapEntry || !message.features.empty() || message.legacyJsonConflicts) {
            std::string options;
            if (message.mapEntry) {
                putInt(options, 7, 1);
            }
            if (message.legacyJsonConflicts) {
                putInt(options, 11, 1);
            }
            if (!message.features.empty()) {
                putMessage(options, 12, featuresDescriptor(message.features));
            }
            putMessage(out, 7, options);
        }
        for (const OneofDef& oneof : message.oneofs) {
            std::string decl;
            putString(decl, 1, oneof.name);
            putFeatureOptions(decl, 2, 1, oneof.features);
            putMessage(out, 8, decl);
        }
        for (const ReservedRange& range : message.reservedRanges) {
            putMessage(out, 9, rangeDescriptor(range));
        }
        for (const std::string& name : message.reservedNames) {
            putString(out, 10, name);
        }
        if (message.visibility != Visibility::Unset) {
            putInt(out, 11, (int)message.visibility);
        }
        return out;
    }
    //=============================================================================
    std::string
    fieldDescriptor(const FieldDef& f)
    {
        std::string field;
        putString(field, 1, f.name);
        if (!f.extendee.empty()) {
            putString(field, 2, f.extendee);
        }
        putInt(field, 3, f.number);
        putInt(field, 4, (int)f.label);
        putInt(field, 5, f.isGroup ? (int)FieldType::Group : (int)f.type);
        if (!f.typeName.empty()) {
            putString(field, 6, f.typeName);
        }
        if (f.hasDefault) {
            putString(field, 7, f.defaultValue);
        }
        if (f.hasPackedOption || !f.features.empty()) {
            std::string options;
            if (f.hasPackedOption) {
                putInt(options, 2, f.packedOption ? 1 : 0);
            }
            if (!f.features.empty()) {
                putMessage(options, 21, featuresDescriptor(f.features));
            }
            putMessage(field, 8, options);
        }
        if (f.oneofIndex >= 0) {
            putInt(field, 9, f.oneofIndex);
        }
        putString(field, 10, f.jsonName);
        if (f.proto3Optional) {
            putInt(field, 17, 1);
        }
        return field;
    }
    //=============================================================================
    std::string
    fileDescriptor(const Schema& schema, const FileDef& file)
    {
        std::string out;
        putString(out, 1, file.name);
        if (!file.package.empty()) {
            putString(out, 2, file.package);
        }
        for (const std::string& dependency : file.dependencies) {
            putString(out, 3, dependency);
        }
        putFeatureOptions(out, 8, 50, file.features);
        for (int32_t m : file.messages) {
            putMessage(out, 4, messageDescriptor(schema, m));
        }
        for (int32_t e : file.enums) {
            putMessage(out, 5, enumDescriptor(schema.enums[(size_t)e]));
        }
        for (int32_t x : file.extensions) {
            putMessage(out, 7, fieldDescriptor(schema.extensions[(size_t)x].field));
        }
        for (int32_t s : file.services) {
            const ServiceDef& service = schema.services[(size_t)s];
            std::string body;
            putString(body, 1, service.name);
            for (const MethodDef& method : service.methods) {
                std::string m;
                putString(m, 1, method.name);
                putString(m, 2, method.inputType);
                putString(m, 3, method.outputType);
                if (method.clientStreaming) {
                    putInt(m, 5, 1);
                }
                if (method.serverStreaming) {
                    putInt(m, 6, 1);
                }
                putMessage(body, 2, m);
            }
            putMessage(out, 6, body);
        }
        for (size_t k = 0; k < file.dependencies.size(); ++k) {
            if (std::find(file.publicDependencies.begin(), file.publicDependencies.end(),
                    file.dependencies[k])
                != file.publicDependencies.end()) {
                putInt(out, 10, (int64_t)k);
            }
        }
        putString(out, 12, file.syntax);
        if (!file.edition.empty()) {
            putInt(out, 14, file.edition == "2023" ? 1000 : 1001);
        }
        for (const std::string& dependency : file.optionDependencies) {
            putString(out, 15, dependency);
        }
        return out;
    }
    //=============================================================================
    std::string
    featuresDescriptor(const FeatureSet& features)
    {
        const int8_t values[] = { features.fieldPresence, features.enumType,
            features.repeatedFieldEncoding, features.utf8Validation, features.messageEncoding,
            features.jsonFormat, features.enforceNamingStyle, features.defaultSymbolVisibility };
        std::string out;
        for (int k = 0; k < 8; ++k) {
            if (values[k] != 0) {
                putInt(out, k + 1, values[k]);
            }
        }
        return out;
    }
    //=============================================================================
    void
    putFeatureOptions(
        std::string& out, int32_t number, int32_t featuresNumber, const FeatureSet& features)
    {
        if (features.empty()) {
            return;
        }
        std::string options;
        putMessage(options, featuresNumber, featuresDescriptor(features));
        putMessage(out, number, options);
    }
    //=============================================================================
    std::vector<std::string>
    featureTexts(const FeatureSet& features)
    {
        struct Item
        {
            const char* name;
            int8_t value;
            std::vector<const char*> names; // by value, from 1
        };
        const Item items[] = {
            { "field_presence", features.fieldPresence,
                { "EXPLICIT", "IMPLICIT", "LEGACY_REQUIRED" } },
            { "enum_type", features.enumType, { "OPEN", "CLOSED" } },
            { "repeated_field_encoding", features.repeatedFieldEncoding, { "PACKED", "EXPANDED" } },
            { "utf8_validation", features.utf8Validation, { "", "VERIFY", "NONE" } },
            { "message_encoding", features.messageEncoding, { "LENGTH_PREFIXED", "DELIMITED" } },
            { "json_format", features.jsonFormat, { "ALLOW", "LEGACY_BEST_EFFORT" } },
            { "enforce_naming_style", features.enforceNamingStyle,
                { "STYLE2024", "STYLE_LEGACY" } },
            { "default_symbol_visibility", features.defaultSymbolVisibility,
                { "EXPORT_ALL", "EXPORT_TOP_LEVEL", "LOCAL_ALL", "STRICT" } },
        };
        std::vector<std::string> out;
        for (const Item& item : items) {
            if (item.value >= 1 && (size_t)item.value <= item.names.size()) {
                out.push_back(std::string("features.") + item.name + " = "
                    + item.names[(size_t)item.value - 1]);
            }
        }
        return out;
    }
    //=============================================================================
    int64_t
    extensionGroup(const DescField& f, const std::vector<DescMessage>& nested)
    {
        if (f.type != (int)FieldType::Group) {
            return -1;
        }
        const std::string name = f.typeName.substr(f.typeName.find_last_of('.') + 1);
        for (size_t j = 0; j < nested.size(); ++j) {
            if (nested[j].name == name) {
                return (int64_t)j;
            }
        }
        return -1;
    }
    //=============================================================================
    void
    extendText(std::string& out, const std::vector<DescField>& extensions,
        const std::vector<DescMessage>& nested, const std::string& pad, bool editions, bool proto2)
    {
        for (size_t k = 0; k < extensions.size(); ++k) {
            const DescField& f = extensions[k];
            if (k == 0 || extensions[k - 1].extendee != f.extendee) {
                out += pad + "extend " + f.extendee + " {\n";
            }
            std::string label = f.label == 3 ? "repeated " : (f.proto3Optional ? "optional " : "");
            if (editions && f.label != 3) {
                label.clear();
            }
            if (proto2) {
                label = f.label == 3 ? "repeated " : (f.label == 2 ? "required " : "optional ");
            }
            const int64_t group = proto2 ? extensionGroup(f, nested) : -1;
            if (group >= 0) {
                const DescMessage& message = nested[(size_t)group];
                messageText(out, message, pad + "  ", editions, proto2,
                    pad + "  " + label + "group " + message.name + " = " + std::to_string(f.number)
                        + fieldOptions(f, proto2) + " {\n");
            } else {
                out += pad + "  " + label + fieldTypeText(f) + " " + f.name + " = "
                    + std::to_string(f.number) + fieldOptions(f, proto2) + ";\n";
            }
            if (k + 1 == extensions.size() || extensions[k + 1].extendee != f.extendee) {
                out += pad + "}\n";
            }
        }
    }
    //=============================================================================
    std::string
    escapedLiteral(const std::string& text)
    {
        std::string out;
        for (unsigned char c : text) {
            switch (c) {
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char octal[5];
                    std::snprintf(octal, sizeof(octal), "\\%03o", c);
                    out += octal;
                } else {
                    out.push_back((char)c);
                }
            }
        }
        return out;
    }
    //=============================================================================
    std::string
    visibilityText(int32_t visibility)
    {
        return visibility == 1 ? "local " : (visibility == 2 ? "export " : "");
    }
    //=============================================================================
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
