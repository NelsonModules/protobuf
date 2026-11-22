//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoSchema.hpp"
#include "ProtoWellKnown.hpp"
#include <set>
#include <algorithm>
#include <climits>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_set>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    //=============================================================================
    constexpr int32_t kMaxFieldNumber = 536870911;
    constexpr int32_t kFirstImplementationNumber = 19000;
    constexpr int32_t kLastImplementationNumber = 19999;
    // Elements a feature can be set on.
    constexpr int kTargetFile = 1;
    constexpr int kTargetMessage = 2;
    constexpr int kTargetField = 4;
    constexpr int kTargetOneof = 8;
    constexpr int kTargetEnum = 16;
    constexpr int kTargetEnumValue = 32;
    //=============================================================================
    enum class TokenKind
    {
        Identifier,
        Integer,
        Float,
        String,
        Symbol,
        End
    };
    //=============================================================================
    struct Token
    {
        TokenKind kind = TokenKind::End;
        std::string text;
        int line = 0;
        int column = 0;
    };
    //=============================================================================
    enum class OptionValueKind
    {
        Identifier,
        Integer,
        Float,
        String,
        Aggregate
    };
    //=============================================================================
    struct OptionValue
    {
        OptionValueKind kind = OptionValueKind::Identifier;
        std::string text;
    };
    //=============================================================================
    enum class SymbolKind
    {
        Package,
        Message,
        Enum,
        EnumValue,
        Field,
        Oneof,
        Service,
        Method
    };
    //=============================================================================
    std::string
    joinName(const std::string& scope, const std::string& name);
    std::string
    toJsonName(const std::string& name);
    std::string
    mapEntryName(const std::string& fieldName);
    bool
    scalarTypeFromKeyword(const std::string& word, FieldType& type);
    bool
    isValidMapKeyType(FieldType type);
    uint64_t
    parseIntegerText(const std::string& text, bool& overflow);
    void
    appendUtf8(std::string& out, uint32_t codePoint);
    int
    hexValue(char c);
    // Features of a file before any option: proto3 rules, or the defaults of an edition.
    FeatureSet
    editionDefaults(const std::string& edition);
    // Key of editionDefaults for a file: "proto2", "proto3", "2023" or "2024".
    std::string
    featureKey(const FileDef& file);
    // Text format string literal: quoted, with C escapes.
    std::string
    textFormatString(const std::string& text);
    // Text format string literal: quoted, with C escapes.
    std::string
    textFormatString(const std::string& text);
    // Shortest text that reads back the same value, like the reference descriptor.
    std::string
    simpleDouble(double value);
    std::string
    simpleFloat(float value);
    // Bytes default value as the reference descriptor stores it (C escapes, octal).
    std::string
    cEscape(const std::string& bytes);
    // Naming rule broken by a name (edition 2024), Ok when it follows the style.
    enum class NamingRule
    {
        Ok,
        TitleCase,
        CapitalFirst,
        LowerFirst,
        LowerSnake,
        UpperSnake,
        Underscores
    };
    // Naming checks of edition 2024.
    NamingRule
    titleCaseError(const std::string& name);
    NamingRule
    lowerSnakeError(const std::string& name);
    NamingRule
    upperSnakeError(const std::string& name);
    // Snake case with letters of one case: an underscore must be followed by a letter.
    NamingRule
    snakeError(const std::string& name, bool upper);
    //=============================================================================
    class Lexer
    {
    public:
        Lexer(const std::string& text, const std::string& fileName) : text(text), fileName(fileName)
        {
            if (this->text.size() >= 3 && (unsigned char)this->text[0] == 0xEF
                && (unsigned char)this->text[1] == 0xBB && (unsigned char)this->text[2] == 0xBF) {
                pos = 3;
            }
        }
        std::vector<Token>
        tokenize();

    private:
        const std::string& text;
        const std::string& fileName;
        size_t pos = 0;
        int line = 1;
        int column = 1;
        char
        current(size_t offset = 0) const
        {
            return pos + offset < text.size() ? text[pos + offset] : '\0';
        }
        void
        advance();
        void
        skipSpaceAndComments();
        Token
        readNumber();
        std::string
        readString();
    };
    //=============================================================================
    class Parser
    {
    public:
        Parser(std::vector<Token> tokens, const std::string& fileName, Schema& schema)
            : tokens(std::move(tokens))
            , fileName(fileName)
            , schema(schema)
            , fileIndex((int32_t)schema.files.size())
        {
        }
        void
        parseFile();
        // Import statements of the file, with their positions for error messages.
        struct ImportRef
        {
            std::string name;
            int line;
            int column;
        };
        std::vector<ImportRef> imports;

    private:
        std::vector<Token> tokens;
        size_t index = 0;
        const std::string& fileName;
        Schema& schema;
        int32_t fileIndex;
        // "2023" or "2024" in an editions file, empty in a proto2 or proto3 file.
        std::string edition;
        bool legacy = false; // proto2 file
        bool
        proto2() const
        {
            return legacy;
        }
        bool
        editions() const
        {
            return !edition.empty();
        }
        const Token&
        peek(size_t offset = 0) const
        {
            size_t i = std::min(index + offset, tokens.size() - 1);
            return tokens[i];
        }
        const Token&
        next()
        {
            const Token& t = tokens[index];
            if (index < tokens.size() - 1) {
                index++;
            }
            return t;
        }
        bool
        atEnd() const
        {
            return peek().kind == TokenKind::End;
        }
        bool
        isSymbol(const char* symbol, size_t offset = 0) const
        {
            return peek(offset).kind == TokenKind::Symbol && peek(offset).text == symbol;
        }
        bool
        isIdentifier(const char* word, size_t offset = 0) const
        {
            return peek(offset).kind == TokenKind::Identifier && peek(offset).text == word;
        }
        bool
        accept(const char* symbol)
        {
            if (isSymbol(symbol)) {
                next();
                return true;
            }
            return false;
        }
        // Place of a token, for error messages.
        Location
        at(const Token& token) const
        {
            return Location { fileName, token.line, token.column };
        }
        // "expected a field name." and the like (what: "a field name").
        [[noreturn]] void
        failExpected(const std::string& what) const;
        void
        expect(const char* symbol);
        const Token&
        expectIdentifier(const char* what);
        void
        expectKeyword(const char* word);
        std::string
        parseFullIdent();
        std::string
        parseTypeName();
        std::string
        parseStringLiteral();
        int64_t
        parseSignedInteger(bool allowNegative);
        int32_t
        parseFieldNumber();
        std::string
        parseOptionName();
        OptionValue
        parseOptionValue();
        std::pair<std::string, OptionValue>
        parseOptionStatement();
        void
        parseFieldType(FieldDef& field);
        void
        parseFieldOptions(FieldDef& field);
        // "features.name = VALUE" is stored in features; false for any other option.
        bool
        applyFeature(const std::string& name, const OptionValue& value, FeatureSet& features,
            int target, const Token& at);
        void
        parseDefaultValue(FieldDef& field, const OptionValue& value, const Token& at);
        // "export" or "local" before a message or an enum (edition 2024), consumed when present.
        Visibility
        parseVisibility();
        int32_t
        parseMessage(const std::string& scope);
        // Message whose name was already read (a message, or the type of a proto2 group).
        int32_t
        parseMessageNamed(const std::string& scope, const Token& nameToken);
        // proto2 "label group Name = number { ... }": a nested message and its field.
        void
        parseGroup(int32_t messageIndex, int32_t oneofIndex, FieldDef& field);
        // Group name, number, options and message; returns the index of the message.
        int32_t
        parseGroupHead(const std::string& scope, FieldDef& field);
        // optional / repeated / required before a field outside a oneof.
        void
        parseLabel(FieldDef& field);
        // "extend Type { fields }" in a file (scopeMessage -1) or a message.
        void
        parseExtend(int32_t scopeMessage, std::vector<int32_t>* fileMessages,
            std::vector<int32_t>* fileExtensions);
        // "extensions 100 to 199, 500 to max [options];"
        void
        parseExtensionRanges(int32_t messageIndex);
        void
        parseMessageBody(int32_t messageIndex);
        void
        parseField(int32_t messageIndex, int32_t oneofIndex);
        void
        parseOneof(int32_t messageIndex);
        void
        parseMapField(int32_t messageIndex);
        void
        parseReserved(
            std::vector<ReservedRange>& ranges, std::vector<std::string>& names, bool isEnum);
        int32_t
        parseEnum(const std::string& scope);
        int32_t
        parseService(const std::string& scope);
        void
        addSyntheticOneofs(int32_t messageIndex);
    };
    //=============================================================================
    class Resolver
    {
    public:
        explicit Resolver(Schema& schema, bool checkNaming = true)
            : schema(schema), checkNaming(checkNaming)
        {
        }
        void
        run();

    private:
        Schema& schema;
        bool checkNaming;
        // File of the definition being checked, for error messages.
        std::string fileName;
        void
        enterFile(int32_t file)
        {
            fileName = schema.files[(size_t)file].path;
        }
        std::unordered_map<std::string, SymbolKind> symbols;
        void
        addSymbol(const std::string& name, SymbolKind kind, int line, int column);
        void
        addPackage(const std::string& package);
        std::string
        lookupType(
            const std::string& typeName, const std::string& relativeTo, int line, int column);
        void
        resolveField(int32_t file, const std::string& relativeTo, FieldDef& field);
        // Extended message, field type and rules of the extensions; appends them to the fields
        // of the extended messages.
        void
        resolveExtensions();
        void
        buildLookup(MessageDef& message);
        void
        validateMessage(MessageDef& message);
        void
        validateEnum(const EnumDef& enumDef);
        void
        finalizeField(MessageDef& message, FieldDef& field);
        void
        resolveMessageFeatures(int32_t messageIndex, const FeatureSet& parent);
        // Edition 2024 naming style of the definitions of one file.
        void
        checkNamingStyle(const FileDef& file);
        // A type of another file must be exported by it (edition 2024 visibility).
        void
        checkVisible(int32_t fromFile, const std::string& fullName, int line, int column);
    };
    //=============================================================================
    // Parses a file and, depth first, the files it imports (each once).
    class ImportLoader
    {
    public:
        ImportLoader(std::vector<std::string> importPaths, bool useFiles,
            const std::map<std::string, std::string>* memoryFiles = nullptr)
            : importPaths(std::move(importPaths)), useFiles(useFiles), memoryFiles(memoryFiles)
        {
        }
        Schema schema;
        void
        load(const std::string& text, const std::string& path, const std::string& name,
            const std::string& folder);

    private:
        std::vector<std::string> importPaths;
        bool useFiles;
        const std::map<std::string, std::string>* memoryFiles;
        std::set<std::string> loaded;
        std::vector<std::string> active;
        bool
        find(const std::string& name, const std::string& folder, std::string& text,
            std::string& path, std::string& foundFolder) const;
    };
    //=============================================================================
    bool
    readTextFile(const std::string& path, std::string& text);
    //=============================================================================
} // namespace
//=============================================================================
FeatureSet
FeatureSet::over(const FeatureSet& base) const
{
    FeatureSet out = base;
    auto take = [](int8_t mine, int8_t& target) {
        if (mine != 0) {
            target = mine;
        }
    };
    take(fieldPresence, out.fieldPresence);
    take(enumType, out.enumType);
    take(repeatedFieldEncoding, out.repeatedFieldEncoding);
    take(utf8Validation, out.utf8Validation);
    take(messageEncoding, out.messageEncoding);
    take(jsonFormat, out.jsonFormat);
    take(enforceNamingStyle, out.enforceNamingStyle);
    take(defaultSymbolVisibility, out.defaultSymbolVisibility);
    return out;
}
//=============================================================================
int32_t
MessageDef::findField(int32_t number) const
{
    if (number >= 0 && (size_t)number < denseLookup.size()) {
        return denseLookup[(size_t)number];
    }
    auto it = sparseLookup.find(number);
    return it == sparseLookup.end() ? -1 : it->second;
}
//=============================================================================
int32_t
MessageDef::findFieldByName(const std::string& fieldName) const
{
    for (size_t k = 0; k < fields.size(); ++k) {
        if (fields[k].name == fieldName) {
            return (int32_t)k;
        }
    }
    return -1;
}
//=============================================================================
int32_t
Schema::findMessage(const std::string& fullName) const
{
    const std::string key
        = (!fullName.empty() && fullName[0] == '.') ? fullName.substr(1) : fullName;
    auto it = messageByName.find(key);
    return it == messageByName.end() ? -1 : it->second;
}
//=============================================================================
int32_t
Schema::findEnum(const std::string& fullName) const
{
    const std::string key
        = (!fullName.empty() && fullName[0] == '.') ? fullName.substr(1) : fullName;
    auto it = enumByName.find(key);
    return it == enumByName.end() ? -1 : it->second;
}
//=============================================================================
std::vector<std::string>
Schema::messageNames() const
{
    std::vector<std::string> names;
    for (const MessageDef& m : messages) {
        if (!m.mapEntry) {
            names.push_back(m.fullName);
        }
    }
    return names;
}
//=============================================================================
int32_t
Schema::resolveMessage(const std::string& name) const
{
    int32_t exact = findMessage(name);
    if (exact >= 0 && !messages[(size_t)exact].mapEntry) {
        return exact;
    }
    const std::string suffix = "." + name;
    int32_t found = -1;
    for (size_t k = 0; k < messages.size(); ++k) {
        const std::string& full = messages[k].fullName;
        if (messages[k].mapEntry || full.size() <= suffix.size()
            || full.compare(full.size() - suffix.size(), suffix.size(), suffix) != 0) {
            continue;
        }
        if (found >= 0) {
            Error("protobuf:parseError:messageNameAmbiguous",
                "message name \"%s\" is ambiguous (\"%s\", \"%s\").", name,
                messages[(size_t)found].fullName, full);
        }
        found = (int32_t)k;
    }
    if (found < 0) {
        Error("protobuf:parseError:unknownMessageType", "unknown message type \"%s\".", name);
    }
    return found;
}
//=============================================================================
Schema
parseProtoText(const std::string& text, const std::string& fileName,
    const std::vector<std::string>& importPaths)
{
    ImportLoader loader(importPaths, !importPaths.empty());
    loader.load(text, fileName, fileName, importPaths.empty() ? "" : importPaths[0]);
    Resolver resolver(loader.schema);
    resolver.run();
    return std::move(loader.schema);
}
//=============================================================================
Schema
parseProtoTexts(
    const std::map<std::string, std::string>& files, const std::string& root, bool checkNaming)
{
    auto it = files.find(root);
    if (it == files.end()) {
        Error("protobuf:parseError:noSuchFile", "%s: no such file.", root);
    }
    ImportLoader loader({ }, false, &files);
    loader.load(it->second, root, root, "");
    Resolver resolver(loader.schema, checkNaming);
    resolver.run();
    return std::move(loader.schema);
}
//=============================================================================
Schema
parseProtoFile(const std::string& path, const std::vector<std::string>& importPaths)
{
    std::string text;
    if (!readTextFile(path, text)) {
        Error("protobuf:parseError:cannotOpenFile", "%s: cannot open the file.", path);
    }
    std::string baseName = path;
    size_t slash = baseName.find_last_of("/\\");
    if (slash != std::string::npos) {
        baseName = baseName.substr(slash + 1);
    }
    ImportLoader loader(importPaths, true);
    loader.load(text, path, baseName, slash == std::string::npos ? "" : path.substr(0, slash));
    Resolver resolver(loader.schema);
    resolver.run();
    return std::move(loader.schema);
}
//=============================================================================
//=============================================================================
ValueKind
valueKindOf(FieldType type)
{
    switch (type) {
    case FieldType::Double:
        return ValueKind::Double;
    case FieldType::Float:
        return ValueKind::Float;
    case FieldType::Int64:
    case FieldType::SInt64:
    case FieldType::SFixed64:
        return ValueKind::Int64;
    case FieldType::UInt64:
    case FieldType::Fixed64:
        return ValueKind::UInt64;
    case FieldType::Int32:
    case FieldType::SInt32:
    case FieldType::SFixed32:
    case FieldType::Enum:
        return ValueKind::Int32;
    case FieldType::UInt32:
    case FieldType::Fixed32:
        return ValueKind::UInt32;
    case FieldType::Bool:
        return ValueKind::Bool;
    case FieldType::String:
    case FieldType::Bytes:
        return ValueKind::Bytes;
    case FieldType::Message:
    case FieldType::Group:
    default:
        return ValueKind::Message;
    }
}
//=============================================================================
bool
isPackableType(FieldType type)
{
    return type != FieldType::String && type != FieldType::Bytes && type != FieldType::Message
        && type != FieldType::Group;
}
//=============================================================================
std::string
fieldTypeName(FieldType type)
{
    static const char* names[] = { "", "double", "float", "int64", "uint64", "int32", "fixed64",
        "fixed32", "bool", "string", "group", "message", "bytes", "uint32", "enum", "sfixed32",
        "sfixed64", "sint32", "sint64" };
    const int index = (int)type;
    return index > 0 && index < (int)(sizeof(names) / sizeof(names[0])) ? names[index] : "";
}
//=============================================================================
namespace {
    //=============================================================================
    void
    ImportLoader::load(const std::string& text, const std::string& path, const std::string& name,
        const std::string& folder)
    {
        loaded.insert(name);
        active.push_back(name);
        Lexer lexer(text, path);
        Parser parser(lexer.tokenize(), path, schema);
        parser.parseFile();
        schema.files.back().name = name;
        schema.files.back().path = path;
        for (const Parser::ImportRef& import : parser.imports) {
            if (std::find(active.begin(), active.end(), import.name) != active.end()) {
                Error("protobuf:parseError:importCycle",
                    "import cycle: \"%s\" imports itself through its imports.",
                    Location { path, import.line, import.column }, import.name);
            }
            if (loaded.count(import.name) != 0) {
                continue;
            }
            std::string importText;
            std::string importPath;
            std::string importFolder;
            if (!find(import.name, folder, importText, importPath, importFolder)) {
                Error("protobuf:parseError:importNotFound", "import \"%s\" not found.",
                    Location { path, import.line, import.column }, import.name);
            }
            load(importText, importPath, import.name, importFolder);
        }
        active.pop_back();
    }
    //=============================================================================
    bool
    ImportLoader::find(const std::string& name, const std::string& folder, std::string& text,
        std::string& path, std::string& foundFolder) const
    {
        if (memoryFiles != nullptr) {
            auto it = memoryFiles->find(name);
            if (it != memoryFiles->end()) {
                text = it->second;
                path = name;
                foundFolder = "";
                return true;
            }
        }
        if (useFiles) {
            std::vector<std::string> folders;
            folders.push_back(folder);
            folders.insert(folders.end(), importPaths.begin(), importPaths.end());
            for (const std::string& dir : folders) {
                std::string candidate = dir.empty() ? name : dir + "/" + name;
                if (readTextFile(candidate, text)) {
                    path = candidate;
                    // Imports of an imported file are relative to its import root.
                    foundFolder = dir;
                    return true;
                }
            }
        }
        const char* builtIn = wellKnownProtoText(name);
        if (builtIn != nullptr) {
            text = builtIn;
            path = "<built-in>/" + name;
            foundFolder = "";
            return true;
        }
        return false;
    }
    //=============================================================================
    bool
    readTextFile(const std::string& path, std::string& text)
    {
        // UTF-8 path, also on Windows.
        std::ifstream stream(std::filesystem::u8path(path), std::ios::binary);
        if (!stream) {
            return false;
        }
        std::ostringstream content;
        content << stream.rdbuf();
        text = content.str();
        return true;
    }
    //=============================================================================
    void
    Lexer::advance()
    {
        if (text[pos] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        pos++;
    }
    //=============================================================================
    void
    Lexer::skipSpaceAndComments()
    {
        while (pos < text.size()) {
            char c = text[pos];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') {
                advance();
            } else if (c == '/' && current(1) == '/') {
                while (pos < text.size() && text[pos] != '\n') {
                    advance();
                }
            } else if (c == '/' && current(1) == '*') {
                int startLine = line;
                int startColumn = column;
                advance();
                advance();
                while (pos < text.size() && !(text[pos] == '*' && current(1) == '/')) {
                    advance();
                }
                if (pos >= text.size()) {
                    Error("protobuf:parseError:unterminatedBlockComment",
                        "unterminated block comment.",
                        Location { fileName, startLine, startColumn });
                }
                advance();
                advance();
            } else {
                break;
            }
        }
    }
    //=============================================================================
    std::vector<Token>
    Lexer::tokenize()
    {
        std::vector<Token> tokens;
        while (true) {
            skipSpaceAndComments();
            Token token;
            token.line = line;
            token.column = column;
            if (pos >= text.size()) {
                token.kind = TokenKind::End;
                tokens.push_back(token);
                break;
            }
            char c = text[pos];
            if (isalpha((unsigned char)c) || c == '_') {
                size_t start = pos;
                while (
                    pos < text.size() && (isalnum((unsigned char)text[pos]) || text[pos] == '_')) {
                    advance();
                }
                token.kind = TokenKind::Identifier;
                token.text = text.substr(start, pos - start);
            } else if (isdigit((unsigned char)c)
                || (c == '.' && isdigit((unsigned char)current(1)))) {
                Token number = readNumber();
                number.line = token.line;
                number.column = token.column;
                token = number;
            } else if (c == '"' || c == '\'') {
                token.kind = TokenKind::String;
                token.text = readString();
            } else if (strchr("=;{}[]()<>,.-+:/", c) != nullptr) {
                token.kind = TokenKind::Symbol;
                token.text = std::string(1, c);
                advance();
            } else {
                Error("protobuf:parseError:unexpectedCharacter", "unexpected character '%s'.",
                    Location { fileName, line, column }, c);
            }
            tokens.push_back(token);
        }
        return tokens;
    }
    //=============================================================================
    Token
    Lexer::readNumber()
    {
        Token token;
        size_t start = pos;
        bool isFloat = false;
        if (current() == '0' && (current(1) == 'x' || current(1) == 'X')) {
            advance();
            advance();
            if (hexValue(current()) < 0) {
                Error("protobuf:parseError:expectedHexadecimalDigits",
                    "expected hexadecimal digits.", Location { fileName, line, column });
            }
            while (hexValue(current()) >= 0) {
                advance();
            }
        } else {
            while (isdigit((unsigned char)current())) {
                advance();
            }
            if (current() == '.') {
                isFloat = true;
                advance();
                while (isdigit((unsigned char)current())) {
                    advance();
                }
            }
            if (current() == 'e' || current() == 'E') {
                isFloat = true;
                advance();
                if (current() == '+' || current() == '-') {
                    advance();
                }
                if (!isdigit((unsigned char)current())) {
                    Error("protobuf:parseError:expectedExponentDigits", "expected exponent digits.",
                        Location { fileName, line, column });
                }
                while (isdigit((unsigned char)current())) {
                    advance();
                }
            }
        }
        if (isalpha((unsigned char)current()) || current() == '_') {
            Error("protobuf:parseError:invalidCharacterNumber", "invalid character in a number.",
                Location { fileName, line, column });
        }
        token.kind = isFloat ? TokenKind::Float : TokenKind::Integer;
        token.text = text.substr(start, pos - start);
        return token;
    }
    //=============================================================================
    std::string
    Lexer::readString()
    {
        char quote = current();
        int startLine = line;
        int startColumn = column;
        advance();
        std::string out;
        while (true) {
            if (pos >= text.size() || current() == '\n') {
                Error("protobuf:parseError:unterminatedStringLiteral",
                    "unterminated string literal.", Location { fileName, startLine, startColumn });
            }
            char c = current();
            if (c == quote) {
                advance();
                break;
            }
            if (c != '\\') {
                out.push_back(c);
                advance();
                continue;
            }
            advance();
            char e = current();
            switch (e) {
            case 'a':
                out.push_back('\a');
                advance();
                break;
            case 'b':
                out.push_back('\b');
                advance();
                break;
            case 'f':
                out.push_back('\f');
                advance();
                break;
            case 'n':
                out.push_back('\n');
                advance();
                break;
            case 'r':
                out.push_back('\r');
                advance();
                break;
            case 't':
                out.push_back('\t');
                advance();
                break;
            case 'v':
                out.push_back('\v');
                advance();
                break;
            case '\\':
            case '\'':
            case '"':
            case '?':
                out.push_back(e);
                advance();
                break;
            case 'x':
            case 'X': {
                advance();
                int value = 0;
                int digits = 0;
                while (digits < 2 && hexValue(current()) >= 0) {
                    value = value * 16 + hexValue(current());
                    advance();
                    digits++;
                }
                if (digits == 0) {
                    Error("protobuf:parseError:expectedHexadecimalDigitsAfterX",
                        "expected hexadecimal digits after \\x.",
                        Location { fileName, line, column });
                }
                out.push_back((char)value);
            } break;
            case 'u':
            case 'U': {
                int count = (e == 'u') ? 4 : 8;
                advance();
                uint32_t value = 0;
                for (int k = 0; k < count; ++k) {
                    if (hexValue(current()) < 0) {
                        Error("protobuf:parseError:invalidUnicodeEscape", "invalid unicode escape.",
                            Location { fileName, line, column });
                    }
                    value = value * 16 + (uint32_t)hexValue(current());
                    advance();
                }
                if (value > 0x10FFFF) {
                    Error("protobuf:parseError:unicodeEscapeOutRange",
                        "unicode escape out of range.", Location { fileName, line, column });
                }
                appendUtf8(out, value);
            } break;
            default:
                if (e >= '0' && e <= '7') {
                    int value = 0;
                    int digits = 0;
                    while (digits < 3 && current() >= '0' && current() <= '7') {
                        value = value * 8 + (current() - '0');
                        advance();
                        digits++;
                    }
                    out.push_back((char)(value & 0xFF));
                } else {
                    Error("protobuf:parseError:invalidEscapeSequence", "invalid escape sequence.",
                        Location { fileName, line, column });
                }
                break;
            }
        }
        return out;
    }
    //=============================================================================
    void
    Parser::expect(const char* symbol)
    {
        if (!accept(symbol)) {
            Error("protobuf:parseError:expectedSymbol", "expected \"%s\".", at(peek()), symbol);
        }
    }
    //=============================================================================
    const Token&
    Parser::expectIdentifier(const char* what)
    {
        if (peek().kind != TokenKind::Identifier) {
            failExpected(what);
        }
        return next();
    }
    //=============================================================================
    void
    Parser::failExpected(const std::string& what) const
    {
        const Location where = at(peek());
        if (what == "a field name") {
            Error("protobuf:parseError:expectedFieldName", "expected a field name.", where);
        }
        if (what == "a group name") {
            Error("protobuf:parseError:expectedGroupName", "expected a group name.", where);
        }
        if (what == "a message name") {
            Error("protobuf:parseError:expectedMessageName", "expected a message name.", where);
        }
        if (what == "a method name") {
            Error("protobuf:parseError:expectedMethodName", "expected a method name.", where);
        }
        if (what == "a oneof name") {
            Error("protobuf:parseError:expectedOneofName", "expected a oneof name.", where);
        }
        if (what == "a reserved name") {
            Error("protobuf:parseError:expectedReservedName", "expected a reserved name.", where);
        }
        if (what == "a service name") {
            Error("protobuf:parseError:expectedServiceName", "expected a service name.", where);
        }
        if (what == "an enum name") {
            Error("protobuf:parseError:expectedEnumName", "expected an enum name.", where);
        }
        if (what == "an enum value name") {
            Error(
                "protobuf:parseError:expectedEnumValueName", "expected an enum value name.", where);
        }
        if (what == "an option name") {
            Error("protobuf:parseError:expectedOptionName", "expected an option name.", where);
        }
        Error("protobuf:parseError:expectedIdentifier", "expected an identifier.", where);
    }
    //=============================================================================
    void
    Parser::expectKeyword(const char* word)
    {
        if (!isIdentifier(word)) {
            Error("protobuf:parseError:expectedSymbol", "expected \"%s\".", at(peek()), word);
        }
        next();
    }
    //=============================================================================
    std::string
    Parser::parseFullIdent()
    {
        std::string name = expectIdentifier("an identifier").text;
        while (isSymbol(".")) {
            next();
            name += "." + expectIdentifier("an identifier").text;
        }
        return name;
    }
    //=============================================================================
    std::string
    Parser::parseTypeName()
    {
        std::string name;
        if (accept(".")) {
            name = ".";
        }
        return name + parseFullIdent();
    }
    //=============================================================================
    std::string
    Parser::parseStringLiteral()
    {
        if (peek().kind != TokenKind::String) {
            Error("protobuf:parseError:expectedStringLiteral", "expected a string literal.",
                at(peek()));
        }
        std::string value = next().text;
        while (peek().kind == TokenKind::String) {
            value += next().text;
        }
        return value;
    }
    //=============================================================================
    int64_t
    Parser::parseSignedInteger(bool allowNegative)
    {
        const Token& start = peek();
        bool negative = false;
        if (isSymbol("-")) {
            if (!allowNegative) {
                Error("protobuf:parseError:expectedPositiveInteger", "expected a positive integer.",
                    at(start));
            }
            next();
            negative = true;
        }
        if (peek().kind != TokenKind::Integer) {
            Error("protobuf:parseError:expectedInteger", "expected an integer.", at(peek()));
        }
        bool overflow = false;
        uint64_t value = parseIntegerText(next().text, overflow);
        if (overflow || value > (uint64_t)INT32_MAX + (negative ? 1 : 0)) {
            Error("protobuf:parseError:integerOutRange", "integer out of range.", at(start));
        }
        return negative ? -(int64_t)value : (int64_t)value;
    }
    //=============================================================================
    int32_t
    Parser::parseFieldNumber()
    {
        return (int32_t)parseSignedInteger(true);
    }
    //=============================================================================
    std::string
    Parser::parseOptionName()
    {
        std::string name;
        do {
            if (!name.empty()) {
                name += ".";
            }
            if (accept("(")) {
                std::string inner = accept(".") ? "." : "";
                inner += parseFullIdent();
                expect(")");
                name += "(" + inner + ")";
            } else {
                name += expectIdentifier("an option name").text;
            }
        } while (accept("."));
        return name;
    }
    //=============================================================================
    OptionValue
    Parser::parseOptionValue()
    {
        OptionValue value;
        if (isSymbol("{")) {
            int depth = 0;
            do {
                const Token& t = next();
                if (t.kind == TokenKind::End) {
                    Error("protobuf:parseError:unterminatedAggregateOptionValue",
                        "unterminated aggregate option value.", at(t));
                }
                if (t.kind == TokenKind::Symbol && t.text == "{") {
                    depth++;
                } else if (t.kind == TokenKind::Symbol && t.text == "}") {
                    depth--;
                }
                // Strings keep their quotes; a minus sign stays next to its number.
                const bool glued = !value.text.empty() && value.text.back() == '-';
                value.text += (value.text.empty() || glued ? "" : " ")
                    + (t.kind == TokenKind::String ? textFormatString(t.text) : t.text);
            } while (depth > 0);
            value.kind = OptionValueKind::Aggregate;
            return value;
        }
        std::string sign;
        if (isSymbol("-") || isSymbol("+")) {
            sign = next().text;
        }
        const Token& t = peek();
        switch (t.kind) {
        case TokenKind::Integer:
            value.kind = OptionValueKind::Integer;
            value.text = sign + next().text;
            break;
        case TokenKind::Float:
            value.kind = OptionValueKind::Float;
            value.text = sign + next().text;
            break;
        case TokenKind::Identifier:
            if (!sign.empty()) {
                if (t.text != "inf" && t.text != "nan") {
                    Error("protobuf:parseError:expectedNumberAfterSign",
                        "expected a number after the sign.", at(t));
                }
                value.kind = OptionValueKind::Float;
                value.text = sign + next().text;
            } else {
                value.kind = OptionValueKind::Identifier;
                value.text = parseFullIdent();
            }
            break;
        case TokenKind::String:
            if (!sign.empty()) {
                Error("protobuf:parseError:expectedNumberAfterSign",
                    "expected a number after the sign.", at(t));
            }
            value.kind = OptionValueKind::String;
            value.text = parseStringLiteral();
            break;
        default:
            Error("protobuf:parseError:expectedOptionValue", "expected an option value.", at(t));
        }
        return value;
    }
    //=============================================================================
    std::pair<std::string, OptionValue>
    Parser::parseOptionStatement()
    {
        std::string name = parseOptionName();
        expect("=");
        OptionValue value = parseOptionValue();
        expect(";");
        return { name, value };
    }
    //=============================================================================
    void
    Parser::parseFile()
    {
        FileDef file;
        file.name = fileName;
        if (isIdentifier("syntax")) {
            next();
            expect("=");
            const Token& valueToken = peek();
            std::string syntax = parseStringLiteral();
            expect(";");
            if (syntax != "proto3" && syntax != "proto2") {
                Error("protobuf:parseError:syntaxSupportedProto2Proto3",
                    "syntax \"%s\" is not supported (proto2 and proto3 are).", at(valueToken),
                    syntax);
            }
            file.syntax = syntax;
            legacy = syntax == "proto2";
        } else if (isIdentifier("edition")) {
            next();
            expect("=");
            const Token& valueToken = peek();
            std::string value = parseStringLiteral();
            expect(";");
            if (value != "2023" && value != "2024") {
                Error("protobuf:parseError:editionSupportedEditions",
                    "edition \"%s\" is not supported (editions 2023 and 2024 are).", at(valueToken),
                    value);
            }
            file.syntax = "editions";
            file.edition = value;
            edition = value;
        } else {
            // No syntax statement: proto2, as for the reference compiler.
            file.syntax = "proto2";
            legacy = true;
        }
        size_t firstMessage = schema.messages.size();
        size_t firstEnum = schema.enums.size();
        size_t firstService = schema.services.size();
        size_t firstExtension = schema.extensions.size();
        bool hasPackage = false;
        while (!atEnd()) {
            if (accept(";")) {
                continue;
            }
            const Token& t = peek();
            if (t.kind != TokenKind::Identifier) {
                Error("protobuf:parseError:expectedTopLevelDefinition",
                    "expected a top-level definition.", at(t));
            }
            const Visibility visibility = parseVisibility();
            if (visibility != Visibility::Unset) {
                if (next().text == "message") {
                    const int32_t m = parseMessage("");
                    schema.messages[(size_t)m].visibility = visibility;
                    file.messages.push_back(m);
                } else {
                    const int32_t e = parseEnum("");
                    schema.enums[(size_t)e].visibility = visibility;
                    file.enums.push_back(e);
                }
                continue;
            }
            if (t.text == "package") {
                if (hasPackage) {
                    Error("protobuf:parseError:multiplePackageDefinitions",
                        "multiple package definitions.", at(t));
                }
                next();
                file.package = parseFullIdent();
                expect(";");
                hasPackage = true;
            } else if (t.text == "import") {
                next();
                bool isPublic = false;
                bool optionOnly = false;
                if (isIdentifier("public")) {
                    next();
                    isPublic = true;
                } else if (isIdentifier("weak")) {
                    next();
                } else if (isIdentifier("option") && peek(1).kind == TokenKind::String) {
                    if (edition != "2024") {
                        Error("protobuf:parseError:importOptionAllowedEdition",
                            "\"import option\" is only allowed from edition 2024.", at(peek()));
                    }
                    next();
                    optionOnly = true;
                }
                const Token& nameToken = peek();
                std::string name = parseStringLiteral();
                expect(";");
                if (optionOnly) {
                    // Custom option definitions: their options are not interpreted.
                    file.optionDependencies.push_back(name);
                    continue;
                }
                file.dependencies.push_back(name);
                if (isPublic) {
                    file.publicDependencies.push_back(name);
                }
                imports.push_back({ name, nameToken.line, nameToken.column });
            } else if (t.text == "option") {
                next();
                const Token& at = peek();
                auto option = parseOptionStatement();
                if (!applyFeature(option.first, option.second, file.features, kTargetFile, at)) {
                    file.options.emplace_back(option.first, option.second.text);
                }
            } else if (t.text == "message") {
                next();
                file.messages.push_back(parseMessage(""));
            } else if (t.text == "enum") {
                next();
                file.enums.push_back(parseEnum(""));
            } else if (t.text == "service") {
                next();
                file.services.push_back(parseService(""));
            } else if (t.text == "extend") {
                parseExtend(-1, &file.messages, &file.extensions);
            } else {
                Error("protobuf:parseError:expectedTopLevelDefinitionFound",
                    "expected a top-level definition, found \"%s\".", at(t), t.text);
            }
        }
        // Names were built relative to the file; prefix them with the package.
        if (!file.package.empty()) {
            for (size_t k = firstMessage; k < schema.messages.size(); ++k) {
                schema.messages[k].fullName = file.package + "." + schema.messages[k].fullName;
            }
            for (size_t k = firstEnum; k < schema.enums.size(); ++k) {
                schema.enums[k].fullName = file.package + "." + schema.enums[k].fullName;
            }
            for (size_t k = firstService; k < schema.services.size(); ++k) {
                schema.services[k].fullName = file.package + "." + schema.services[k].fullName;
            }
            for (size_t k = firstExtension; k < schema.extensions.size(); ++k) {
                schema.extensions[k].fullName = file.package + "." + schema.extensions[k].fullName;
            }
        }
        schema.files.push_back(file);
    }
    //=============================================================================
    void
    Parser::parseFieldType(FieldDef& field)
    {
        const Token& t = peek();
        if (t.kind == TokenKind::Identifier) {
            if (t.text == "group") {
                if (editions()) {
                    Error("protobuf:parseError:groupsNotInEditions",
                        "groups are not allowed in editions; use features.message_encoding = "
                        "DELIMITED.",
                        at(t));
                }
                Error("protobuf:parseError:groupsOnlyInProto2",
                    "groups are only allowed in proto2 files.", at(t));
            }
            FieldType type;
            if (scalarTypeFromKeyword(t.text, type)) {
                next();
                field.type = type;
                return;
            }
        } else if (!isSymbol(".")) {
            Error("protobuf:parseError:expectedFieldType", "expected a field type.", at(t));
        }
        field.type = FieldType::Message;
        field.typeName = parseTypeName();
    }
    //=============================================================================
    void
    Parser::parseFieldOptions(FieldDef& field)
    {
        do {
            const Token& start = peek();
            std::string name = parseOptionName();
            expect("=");
            OptionValue value = parseOptionValue();
            if (name == "packed") {
                if (editions()) {
                    Error("protobuf:parseError:packedOptionAllowedEditionsUse",
                        "the packed option is not allowed in editions; use "
                        "features.repeated_field_encoding.",
                        at(start));
                }
                if (value.kind != OptionValueKind::Identifier
                    || (value.text != "true" && value.text != "false")) {
                    Error("protobuf:parseError:packedOptionExpectsTrueFalse",
                        "the packed option expects true or false.", at(start));
                }
                field.hasPackedOption = true;
                field.packedOption = value.text == "true";
            } else if (name == "json_name") {
                if (value.kind != OptionValueKind::String) {
                    Error("protobuf:parseError:jsonNameOptionExpectsString",
                        "the json_name option expects a string.", at(start));
                }
                field.jsonName = value.text;
                field.hasJsonNameOption = true;
            } else if (name == "default") {
                if (!editions() && !proto2()) {
                    Error("protobuf:parseError:explicitDefaultValuesAllowedProto3",
                        "explicit default values are not allowed in proto3.", at(start));
                }
                parseDefaultValue(field, value, start);
            } else if (!applyFeature(name, value, field.features, kTargetField, start)) {
                field.options.emplace_back(name, value.text);
                field.optionLiterals.push_back(value.kind == OptionValueKind::String
                        ? textFormatString(value.text)
                        : value.text);
            }
        } while (accept(","));
        expect("]");
    }
    //=============================================================================
    bool
    Parser::applyFeature(const std::string& name, const OptionValue& value, FeatureSet& features,
        int target, const Token& at)
    {
        if (name == "features" && value.kind == OptionValueKind::Aggregate) {
            // features = { name: VALUE ... }: each one as features.name = VALUE.
            std::vector<std::string> words;
            std::istringstream stream(value.text);
            for (std::string word; stream >> word;) {
                words.push_back(word);
            }
            for (size_t k = 1; k + 1 < words.size(); ++k) {
                const std::string& word = words[k];
                if (word == "," || word == ";") {
                    continue;
                }
                // Language features ([pb.cpp] { ... } or (pb.cpp) { ... }) are skipped.
                if (word == "[" || word == "(") {
                    const std::string close = word == "[" ? "]" : ")";
                    while (k + 1 < words.size() && words[k] != close) {
                        k++;
                    }
                    if (k + 1 < words.size() && words[k + 1] == ":") {
                        k++;
                    }
                    if (k + 1 < words.size() && words[k + 1] == "{") {
                        int depth = 0;
                        do {
                            k++;
                            depth += words[k] == "{" ? 1 : (words[k] == "}" ? -1 : 0);
                        } while (depth > 0 && k + 1 < words.size());
                    } else {
                        k++;
                    }
                    continue;
                }
                if (k + 2 >= words.size() || words[k + 1] != ":") {
                    Error("protobuf:parseError:invalidFeaturesValue",
                        "invalid features value \"%s\".", Location { fileName, at.line, at.column },
                        value.text);
                }
                OptionValue item;
                item.text = words[k + 2];
                applyFeature("features." + word, item, features, target, at);
                k += 2;
            }
            return true;
        }
        const std::string prefix = "features.";
        if (name.compare(0, prefix.size(), prefix) != 0) {
            return false;
        }
        if (!editions()) {
            Error("protobuf:parseError:featuresAllowedEditionsFiles",
                "features are only allowed in editions files.",
                Location { fileName, at.line, at.column });
        }
        const std::string feature = name.substr(prefix.size());
        if (!feature.empty() && feature[0] == '(') {
            // Features of one language runtime: they do not change the wire format.
            return true;
        }
        struct Spec
        {
            const char* name;
            int8_t FeatureSet::* member;
            int targets;
            const char* edition;
            std::vector<std::pair<const char*, int>> values;
        };
        const int all = kTargetFile | kTargetMessage | kTargetField | kTargetOneof | kTargetEnum
            | kTargetEnumValue;
        static const std::vector<Spec> specs = {
            { "field_presence", &FeatureSet::fieldPresence, kTargetFile | kTargetField, "2023",
                { { "EXPLICIT", 1 }, { "IMPLICIT", 2 }, { "LEGACY_REQUIRED", 3 } } },
            { "enum_type", &FeatureSet::enumType, kTargetFile | kTargetEnum, "2023",
                { { "OPEN", 1 }, { "CLOSED", 2 } } },
            { "repeated_field_encoding", &FeatureSet::repeatedFieldEncoding,
                kTargetFile | kTargetField, "2023", { { "PACKED", 1 }, { "EXPANDED", 2 } } },
            { "utf8_validation", &FeatureSet::utf8Validation, kTargetFile | kTargetField, "2023",
                { { "VERIFY", 2 }, { "NONE", 3 } } },
            { "message_encoding", &FeatureSet::messageEncoding, kTargetFile | kTargetField, "2023",
                { { "LENGTH_PREFIXED", 1 }, { "DELIMITED", 2 } } },
            { "json_format", &FeatureSet::jsonFormat, kTargetFile | kTargetMessage | kTargetEnum,
                "2023", { { "ALLOW", 1 }, { "LEGACY_BEST_EFFORT", 2 } } },
            { "enforce_naming_style", &FeatureSet::enforceNamingStyle, all, "2024",
                { { "STYLE2024", 1 }, { "STYLE_LEGACY", 2 } } },
            { "default_symbol_visibility", &FeatureSet::defaultSymbolVisibility, kTargetFile,
                "2024",
                { { "EXPORT_ALL", 1 }, { "EXPORT_TOP_LEVEL", 2 }, { "LOCAL_ALL", 3 },
                    { "STRICT", 4 } } },
        };
        for (const Spec& spec : specs) {
            if (feature != spec.name) {
                continue;
            }
            if ((spec.targets & target) == 0) {
                Error("protobuf:parseError:featureSetElement",
                    "feature \"%s\" cannot be set on this element.",
                    Location { fileName, at.line, at.column }, feature);
            }
            if (edition < spec.edition) {
                Error("protobuf:parseError:featureNeedsEditionLater",
                    "feature \"%s\" needs edition %s or later.",
                    Location { fileName, at.line, at.column }, feature, spec.edition);
            }
            if (value.kind == OptionValueKind::Identifier) {
                for (const auto& item : spec.values) {
                    if (value.text == item.first) {
                        features.*spec.member = (int8_t)item.second;
                        return true;
                    }
                }
            }
            Error("protobuf:parseError:notAFeatureValue",
                "\"%s\" is not a value of feature \"%s\".",
                Location { fileName, at.line, at.column }, value.text, feature);
        }
        Error("protobuf:parseError:unknownFeature", "unknown feature \"%s\".",
            Location { fileName, at.line, at.column }, feature);
    }
    //=============================================================================
    void
    Parser::parseDefaultValue(FieldDef& field, const OptionValue& value, const Token& at)
    {
        if (field.label == FieldLabel::Repeated) {
            Error("protobuf:parseError:repeatedFieldsDefaultValues",
                "repeated fields cannot have default values.",
                Location { fileName, at.line, at.column });
        }
        field.hasDefault = true;
        const std::string& text = value.text;
        switch (field.type) {
        case FieldType::Message:
            // Enum value name, checked once the type is resolved.
            if (value.kind != OptionValueKind::Identifier) {
                Error("protobuf:parseError:defaultValueEnumFieldValue",
                    "the default value of an enum field must be one of its value names.",
                    Location { fileName, at.line, at.column });
            }
            field.defaultValue = text;
            return;
        case FieldType::String:
        case FieldType::Bytes:
            if (value.kind != OptionValueKind::String) {
                Error("protobuf:parseError:defaultValueStringBytesField",
                    "the default value of a string or bytes field must be a string.",
                    Location { fileName, at.line, at.column });
            }
            field.defaultValue = field.type == FieldType::Bytes ? cEscape(text) : text;
            return;
        case FieldType::Bool:
            if (value.kind != OptionValueKind::Identifier || (text != "true" && text != "false")) {
                Error("protobuf:parseError:defaultValueBoolFieldTrue",
                    "the default value of a bool field must be true or false.",
                    Location { fileName, at.line, at.column });
            }
            field.defaultValue = text;
            return;
        case FieldType::Double:
        case FieldType::Float: {
            double number = 0;
            const bool negative = !text.empty() && text[0] == '-';
            const std::string body
                = text.substr(!text.empty() && (text[0] == '-' || text[0] == '+'));
            if (value.kind == OptionValueKind::Integer) {
                bool overflow = false;
                number = (double)parseIntegerText(body, overflow);
            } else if (value.kind == OptionValueKind::Float && body != "inf" && body != "nan") {
                number = std::strtod(body.c_str(), nullptr);
            } else if (body == "inf" || body == "nan") {
                number = body == "inf" ? INFINITY : NAN;
            } else {
                Error("protobuf:parseError:defaultValueFloatingPointField",
                    "the default value of a floating-point field must be a number.",
                    Location { fileName, at.line, at.column });
            }
            number = negative ? -number : number;
            field.defaultValue = field.type == FieldType::Double ? simpleDouble(number)
                                                                 : simpleFloat((float)number);
            return;
        }
        default: {
            if (value.kind != OptionValueKind::Integer) {
                Error("protobuf:parseError:defaultValueIntegerFieldInteger",
                    "the default value of an integer field must be an integer.",
                    Location { fileName, at.line, at.column });
            }
            const bool negative = text[0] == '-';
            bool overflow = false;
            const uint64_t magnitude
                = parseIntegerText(text.substr(negative || text[0] == '+'), overflow);
            const bool is64 = field.type == FieldType::Int64 || field.type == FieldType::SInt64
                || field.type == FieldType::SFixed64 || field.type == FieldType::UInt64
                || field.type == FieldType::Fixed64;
            const bool isUnsigned = field.type == FieldType::UInt32
                || field.type == FieldType::UInt64 || field.type == FieldType::Fixed32
                || field.type == FieldType::Fixed64;
            const uint64_t limit = isUnsigned
                ? (is64 ? UINT64_MAX : UINT32_MAX)
                : (is64 ? (uint64_t)INT64_MAX : (uint64_t)INT32_MAX) + (negative ? 1 : 0);
            if (overflow || (negative && isUnsigned) || magnitude > limit) {
                Error("protobuf:parseError:defaultValueOutRangeField",
                    "the default value is out of range for the field type.",
                    Location { fileName, at.line, at.column });
            }
            field.defaultValue
                = negative ? "-" + std::to_string(magnitude) : std::to_string(magnitude);
            if (field.defaultValue == "-0") {
                field.defaultValue = "0";
            }
            return;
        }
        }
    }
    //=============================================================================
    Visibility
    Parser::parseVisibility()
    {
        if ((isIdentifier("export") || isIdentifier("local"))
            && (isIdentifier("message", 1) || isIdentifier("enum", 1))
            && peek(2).kind == TokenKind::Identifier) {
            if (edition != "2024") {
                Error("protobuf:parseError:exportLocalAllowedEdition",
                    "\"export\" and \"local\" are only allowed from edition 2024.", at(peek()));
            }
            return next().text == "export" ? Visibility::Export : Visibility::Local;
        }
        return Visibility::Unset;
    }
    //=============================================================================
    int32_t
    Parser::parseMessage(const std::string& scope)
    {
        const Token& nameToken = expectIdentifier("a message name");
        return parseMessageNamed(scope, nameToken);
    }
    //=============================================================================
    void
    Parser::parseLabel(FieldDef& field)
    {
        const Token& start = peek();
        if (isIdentifier("optional") && !isSymbol("=", 2)) {
            if (editions()) {
                Error("protobuf:parseError:optionalLabelAllowedEditionsSingular",
                    "the optional label is not allowed in editions; singular fields have presence "
                    "unless features.field_presence = IMPLICIT.",
                    at(start));
            }
            next();
            field.proto3Optional = !proto2();
        } else if (isIdentifier("repeated") && !isSymbol("=", 2)) {
            next();
            field.label = FieldLabel::Repeated;
        } else if (proto2() && isIdentifier("required") && !isSymbol("=", 2)) {
            next();
            field.label = FieldLabel::Required;
        } else if (proto2()) {
            Error("protobuf:parseError:fieldsProto2FilesNeedLabel",
                "fields of proto2 files need a label: optional, required or repeated.", at(start));
        }
    }
    //=============================================================================
    int32_t
    Parser::parseGroupHead(const std::string& scope, FieldDef& field)
    {
        next();
        const Token& nameToken = expectIdentifier("a group name");
        if (!std::isupper((unsigned char)nameToken.text[0])) {
            Error("protobuf:parseError:groupNamesStartCapitalLetter",
                "group names must start with a capital letter.", at(nameToken));
        }
        expect("=");
        field.number = parseFieldNumber();
        if (accept("[")) {
            parseFieldOptions(field);
        }
        const int32_t child = parseMessageNamed(scope, nameToken);
        // The field is the group name in lower case; its type is the group message.
        std::string name = nameToken.text;
        std::transform(name.begin(), name.end(), name.begin(),
            [](unsigned char c) { return (char)std::tolower(c); });
        field.name = name;
        field.line = nameToken.line;
        field.column = nameToken.column;
        field.type = FieldType::Message;
        field.typeName = nameToken.text;
        field.isGroup = true;
        return child;
    }
    //=============================================================================
    void
    Parser::parseExtensionRanges(int32_t messageIndex)
    {
        next();
        std::vector<ReservedRange> ranges;
        do {
            const Token& start = peek();
            int64_t first = parseSignedInteger(false);
            int64_t last = first;
            if (isIdentifier("to")) {
                next();
                if (isIdentifier("max")) {
                    next();
                    last = kMaxFieldNumber;
                } else {
                    last = parseSignedInteger(false);
                }
            }
            if (first < 1 || last > kMaxFieldNumber) {
                Error("protobuf:parseError:extensionNumbersBetween",
                    "extension numbers must be between 1 and %d.", at(start), kMaxFieldNumber);
            }
            if (last < first) {
                Error("protobuf:parseError:extensionRangeEndNumberGreater",
                    "extension range end number must be greater than start number.", at(start));
            }
            ReservedRange range;
            range.start = (int32_t)first;
            range.end = (int32_t)(last + 1);
            ranges.push_back(range);
        } while (accept(","));
        // Range options (declaration, verification) are read and not kept.
        if (accept("[")) {
            do {
                parseOptionName();
                expect("=");
                parseOptionValue();
            } while (accept(","));
            expect("]");
        }
        expect(";");
        std::vector<ReservedRange>& target = schema.messages[(size_t)messageIndex].extensionRanges;
        target.insert(target.end(), ranges.begin(), ranges.end());
    }
    //=============================================================================
    void
    Parser::parseExtend(int32_t scopeMessage, std::vector<int32_t>* fileMessages,
        std::vector<int32_t>* fileExtensions)
    {
        next();
        const std::string extendee = parseTypeName();
        expect("{");
        if (isSymbol("}")) {
            Error("protobuf:parseError:extendBlockContainLeastField",
                "an extend block must contain at least one field.", at(peek()));
        }
        while (!accept("}")) {
            if (atEnd()) {
                Error("protobuf:parseError:unexpectedEndFileInsideExtend",
                    "unexpected end of file inside an extend block.", at(peek()));
            }
            if (accept(";")) {
                continue;
            }
            const Token& start = peek();
            if (isIdentifier("map") && isSymbol("<", 1)) {
                Error("protobuf:parseError:mapFieldsAllowedExtendBlocks",
                    "map fields are not allowed in extend blocks.", at(start));
            }
            if (isIdentifier("oneof") && peek(1).kind == TokenKind::Identifier
                && isSymbol("{", 2)) {
                Error("protobuf:parseError:oneofsAllowedExtendBlocks",
                    "oneofs are not allowed in extend blocks.", at(start));
            }
            const std::string scope
                = scopeMessage >= 0 ? schema.messages[(size_t)scopeMessage].fullName : "";
            FieldDef field;
            parseLabel(field);
            if (proto2() && isIdentifier("group") && peek(1).kind == TokenKind::Identifier
                && isSymbol("=", 2)) {
                const int32_t child = parseGroupHead(scope, field);
                if (scopeMessage >= 0) {
                    schema.messages[(size_t)scopeMessage].nestedMessages.push_back(child);
                } else {
                    fileMessages->push_back(child);
                }
            } else {
                parseFieldType(field);
                const Token& nameToken = expectIdentifier("a field name");
                field.name = nameToken.text;
                field.line = nameToken.line;
                field.column = nameToken.column;
                expect("=");
                field.number = parseFieldNumber();
                if (accept("[")) {
                    parseFieldOptions(field);
                }
                expect(";");
            }
            field.extendee = extendee;
            ExtensionDef extension;
            extension.file = fileIndex;
            extension.scope = scopeMessage;
            extension.fullName = joinName(scope, field.name);
            extension.field = field;
            const int32_t index = (int32_t)schema.extensions.size();
            schema.extensions.push_back(extension);
            if (scopeMessage >= 0) {
                schema.messages[(size_t)scopeMessage].extensions.push_back(index);
            } else {
                fileExtensions->push_back(index);
            }
        }
    }
    //=============================================================================
    void
    Parser::parseGroup(int32_t messageIndex, int32_t oneofIndex, FieldDef& field)
    {
        const int32_t child = parseGroupHead(schema.messages[(size_t)messageIndex].fullName, field);
        field.oneofIndex = oneofIndex;
        MessageDef& message = schema.messages[(size_t)messageIndex];
        message.nestedMessages.push_back(child);
        if (oneofIndex >= 0) {
            message.oneofs[(size_t)oneofIndex].fields.push_back((int32_t)message.fields.size());
        }
        message.fields.push_back(field);
    }
    //=============================================================================
    int32_t
    Parser::parseMessageNamed(const std::string& scope, const Token& nameToken)
    {
        MessageDef message;
        message.file = fileIndex;
        message.name = nameToken.text;
        message.fullName = joinName(scope, message.name);
        message.line = nameToken.line;
        message.column = nameToken.column;
        int32_t messageIndex = (int32_t)schema.messages.size();
        schema.messages.push_back(message);
        expect("{");
        parseMessageBody(messageIndex);
        addSyntheticOneofs(messageIndex);
        return messageIndex;
    }
    //=============================================================================
    void
    Parser::parseMessageBody(int32_t messageIndex)
    {
        while (!accept("}")) {
            if (atEnd()) {
                Error("protobuf:parseError:unexpectedEndFileInsideMessage",
                    "unexpected end of file inside a message.", at(peek()));
            }
            if (accept(";")) {
                continue;
            }
            const Token& t = peek();
            if (t.kind == TokenKind::Identifier) {
                const Visibility visibility = parseVisibility();
                if (visibility != Visibility::Unset) {
                    const bool isMessage = next().text == "message";
                    const std::string scope = schema.messages[(size_t)messageIndex].fullName;
                    if (isMessage) {
                        const int32_t child = parseMessage(scope);
                        schema.messages[(size_t)child].visibility = visibility;
                        schema.messages[(size_t)messageIndex].nestedMessages.push_back(child);
                    } else {
                        const int32_t child = parseEnum(scope);
                        schema.enums[(size_t)child].visibility = visibility;
                        schema.messages[(size_t)messageIndex].nestedEnums.push_back(child);
                    }
                    continue;
                }
                bool namedBlock = peek(1).kind == TokenKind::Identifier && isSymbol("{", 2);
                if (t.text == "message" && namedBlock) {
                    next();
                    int32_t child = parseMessage(schema.messages[(size_t)messageIndex].fullName);
                    schema.messages[(size_t)messageIndex].nestedMessages.push_back(child);
                    continue;
                }
                if (t.text == "enum" && namedBlock) {
                    next();
                    int32_t child = parseEnum(schema.messages[(size_t)messageIndex].fullName);
                    schema.messages[(size_t)messageIndex].nestedEnums.push_back(child);
                    continue;
                }
                if (t.text == "oneof" && namedBlock) {
                    parseOneof(messageIndex);
                    continue;
                }
                if (t.text == "map" && isSymbol("<", 1)) {
                    parseMapField(messageIndex);
                    continue;
                }
                if (t.text == "reserved" && !isSymbol("=", 1)
                    && peek(1).kind != TokenKind::Identifier) {
                    MessageDef& m = schema.messages[(size_t)messageIndex];
                    parseReserved(m.reservedRanges, m.reservedNames, false);
                    continue;
                }
                if (t.text == "reserved" && peek(1).kind == TokenKind::Identifier
                    && !isSymbol("=", 2)) {
                    if (!editions()) {
                        Error("protobuf:parseError:reservedNamesNotStrings",
                            "reserved names must be string literals (identifiers only in "
                            "editions).",
                            at(peek(1)));
                    }
                    MessageDef& m = schema.messages[(size_t)messageIndex];
                    parseReserved(m.reservedRanges, m.reservedNames, false);
                    continue;
                }
                if (t.text == "option" && !isSymbol("=", 1)
                    && (peek(2).kind != TokenKind::Identifier || isSymbol("(", 1))) {
                    next();
                    const Token& at = peek();
                    auto option = parseOptionStatement();
                    if (option.first == "message_set_wire_format" && option.second.text == "true") {
                        Error("protobuf:parseError:messageSetNotSupported",
                            "message_set_wire_format is not supported.",
                            Location { fileName, at.line, at.column });
                    }
                    if (option.first == "deprecated_legacy_json_field_conflicts") {
                        schema.messages[(size_t)messageIndex].legacyJsonConflicts
                            = option.second.text == "true";
                    }
                    applyFeature(option.first, option.second,
                        schema.messages[(size_t)messageIndex].features, kTargetMessage, at);
                    continue;
                }
                if (t.text == "extensions" && !isSymbol("=", 1)) {
                    if (!editions() && !proto2()) {
                        Error("protobuf:parseError:extensionRangesAllowedProto3",
                            "extension ranges are not allowed in proto3.", at(t));
                    }
                    parseExtensionRanges(messageIndex);
                    continue;
                }
                if (t.text == "extend" && !isSymbol("=", 1)) {
                    parseExtend(messageIndex, nullptr, nullptr);
                    continue;
                }
                if (t.text == "required" && !proto2()) {
                    if (editions()) {
                        Error("protobuf:parseError:requiredNotInEditions",
                            "the required label is not allowed in editions; use "
                            "features.field_presence = LEGACY_REQUIRED.",
                            at(t));
                    }
                    Error("protobuf:parseError:requiredNotInProto3",
                        "required fields are not allowed in proto3.", at(t));
                }
            }
            parseField(messageIndex, -1);
        }
    }
    //=============================================================================
    void
    Parser::parseField(int32_t messageIndex, int32_t oneofIndex)
    {
        FieldDef field;
        const Token& start = peek();
        if (oneofIndex < 0) {
            parseLabel(field);
        } else if ((isIdentifier("optional") || isIdentifier("repeated")
                       || isIdentifier("required"))
            && peek(1).kind == TokenKind::Identifier && !isSymbol("=", 2)) {
            Error("protobuf:parseError:fieldsOneofsLabels",
                "fields in oneofs must not have labels.", at(start));
        }
        if (proto2() && isIdentifier("group") && peek(1).kind == TokenKind::Identifier
            && isSymbol("=", 2)) {
            parseGroup(messageIndex, oneofIndex, field);
            return;
        }
        parseFieldType(field);
        const Token& nameToken = expectIdentifier("a field name");
        field.name = nameToken.text;
        field.line = nameToken.line;
        field.column = nameToken.column;
        expect("=");
        field.number = parseFieldNumber();
        if (accept("[")) {
            parseFieldOptions(field);
        }
        expect(";");
        field.oneofIndex = oneofIndex;
        MessageDef& message = schema.messages[(size_t)messageIndex];
        if (oneofIndex >= 0) {
            message.oneofs[(size_t)oneofIndex].fields.push_back((int32_t)message.fields.size());
        }
        message.fields.push_back(field);
    }
    //=============================================================================
    void
    Parser::parseOneof(int32_t messageIndex)
    {
        next();
        const Token& nameToken = expectIdentifier("a oneof name");
        OneofDef oneof;
        oneof.name = nameToken.text;
        int32_t oneofIndex = (int32_t)schema.messages[(size_t)messageIndex].oneofs.size();
        schema.messages[(size_t)messageIndex].oneofs.push_back(oneof);
        expect("{");
        bool hasField = false;
        while (!accept("}")) {
            if (atEnd()) {
                Error("protobuf:parseError:unexpectedEndFileInsideOneof",
                    "unexpected end of file inside a oneof.", at(peek()));
            }
            if (accept(";")) {
                continue;
            }
            if (isIdentifier("option") && !isSymbol("=", 1)
                && (peek(2).kind != TokenKind::Identifier || isSymbol("(", 1))) {
                next();
                const Token& at = peek();
                auto option = parseOptionStatement();
                applyFeature(option.first, option.second,
                    schema.messages[(size_t)messageIndex].oneofs[(size_t)oneofIndex].features,
                    kTargetOneof, at);
                continue;
            }
            if (isIdentifier("map") && isSymbol("<", 1)) {
                Error("protobuf:parseError:mapFieldsAllowedOneofs",
                    "map fields are not allowed in oneofs.", at(peek()));
            }
            parseField(messageIndex, oneofIndex);
            hasField = true;
        }
        if (!hasField) {
            Error("protobuf:parseError:oneofContainLeastField",
                "a oneof must contain at least one field.", at(nameToken));
        }
    }
    //=============================================================================
    void
    Parser::parseMapField(int32_t messageIndex)
    {
        const Token& start = next();
        expect("<");
        const Token& keyToken = peek();
        FieldType keyType;
        if (keyToken.kind != TokenKind::Identifier || !scalarTypeFromKeyword(keyToken.text, keyType)
            || !isValidMapKeyType(keyType)) {
            Error("protobuf:parseError:invalidMapKeyType", "invalid map key type.", at(keyToken));
        }
        next();
        expect(",");
        FieldDef value;
        parseFieldType(value);
        expect(">");
        FieldDef field;
        const Token& nameToken = expectIdentifier("a field name");
        field.name = nameToken.text;
        field.line = nameToken.line;
        field.column = nameToken.column;
        expect("=");
        field.number = parseFieldNumber();
        if (accept("[")) {
            parseFieldOptions(field);
        }
        expect(";");
        if (field.hasPackedOption) {
            Error("protobuf:parseError:packedOptionAllowedMapFields",
                "the packed option is not allowed on map fields.", at(start));
        }
        if (field.hasDefault) {
            Error("protobuf:parseError:mapFieldsDefaultValues",
                "map fields cannot have default values.", at(start));
        }
        MessageDef entry;
        entry.name = mapEntryName(field.name);
        entry.fullName = joinName(schema.messages[(size_t)messageIndex].fullName, entry.name);
        entry.mapEntry = true;
        entry.line = start.line;
        entry.column = start.column;
        FieldDef key;
        key.name = "key";
        key.number = 1;
        key.type = keyType;
        key.line = keyToken.line;
        key.column = keyToken.column;
        value.name = "value";
        value.number = 2;
        value.line = nameToken.line;
        value.column = nameToken.column;
        entry.fields.push_back(key);
        entry.fields.push_back(value);
        int32_t entryIndex = (int32_t)schema.messages.size();
        schema.messages.push_back(entry);
        field.label = FieldLabel::Repeated;
        field.type = FieldType::Message;
        field.messageIndex = entryIndex;
        field.isMap = true;
        MessageDef& message = schema.messages[(size_t)messageIndex];
        message.nestedMessages.push_back(entryIndex);
        message.fields.push_back(field);
    }
    //=============================================================================
    void
    Parser::parseReserved(
        std::vector<ReservedRange>& ranges, std::vector<std::string>& names, bool isEnum)
    {
        next();
        if (peek().kind == TokenKind::String) {
            if (editions()) {
                Error("protobuf:parseError:reservedNamesIdentifiersEditions",
                    "reserved names must be identifiers in editions.", at(peek()));
            }
            do {
                names.push_back(parseStringLiteral());
            } while (accept(","));
            expect(";");
            return;
        }
        if (peek().kind == TokenKind::Identifier) {
            do {
                names.push_back(expectIdentifier("a reserved name").text);
            } while (accept(","));
            expect(";");
            return;
        }
        do {
            const Token& start = peek();
            int64_t first = parseSignedInteger(isEnum);
            int64_t last = first;
            if (isIdentifier("to")) {
                next();
                if (isIdentifier("max")) {
                    next();
                    last = isEnum ? INT32_MAX : kMaxFieldNumber;
                } else {
                    last = parseSignedInteger(isEnum);
                }
            }
            if (!isEnum && first < 1) {
                Error("protobuf:parseError:reservedNumbersPositiveIntegers",
                    "reserved numbers must be positive integers.", at(start));
            }
            if (last < first) {
                Error("protobuf:parseError:reservedRangeEndNumberGreater",
                    "reserved range end number must be greater than start number.", at(start));
            }
            ReservedRange range;
            range.start = (int32_t)first;
            range.end = isEnum ? (int32_t)last : (int32_t)(last + 1);
            ranges.push_back(range);
        } while (accept(","));
        expect(";");
    }
    //=============================================================================
    int32_t
    Parser::parseEnum(const std::string& scope)
    {
        const Token& nameToken = expectIdentifier("an enum name");
        EnumDef enumDef;
        enumDef.file = fileIndex;
        enumDef.name = nameToken.text;
        enumDef.fullName = joinName(scope, enumDef.name);
        enumDef.line = nameToken.line;
        enumDef.column = nameToken.column;
        int32_t enumIndex = (int32_t)schema.enums.size();
        schema.enums.push_back(enumDef);
        expect("{");
        while (!accept("}")) {
            if (atEnd()) {
                Error("protobuf:parseError:unexpectedEndFileInsideEnum",
                    "unexpected end of file inside an enum.", at(peek()));
            }
            if (accept(";")) {
                continue;
            }
            EnumDef& current = schema.enums[(size_t)enumIndex];
            if (isIdentifier("option") && !isSymbol("=", 1)) {
                next();
                const Token& at = peek();
                auto option = parseOptionStatement();
                if (option.first == "allow_alias") {
                    current.allowAlias = option.second.text == "true";
                } else {
                    applyFeature(option.first, option.second, current.features, kTargetEnum, at);
                }
                continue;
            }
            if (isIdentifier("reserved") && !isSymbol("=", 1)) {
                if (peek(1).kind == TokenKind::Identifier && !editions()) {
                    Error("protobuf:parseError:reservedNamesNotStrings",
                        "reserved names must be string literals (identifiers only in editions).",
                        at(peek(1)));
                }
                parseReserved(current.reservedRanges, current.reservedNames, true);
                continue;
            }
            const Token& valueToken = expectIdentifier("an enum value name");
            expect("=");
            EnumValueDef value;
            value.name = valueToken.text;
            value.number = (int32_t)parseSignedInteger(true);
            if (accept("[")) {
                do {
                    const Token& at = peek();
                    const std::string optionName = parseOptionName();
                    expect("=");
                    const OptionValue optionValue = parseOptionValue();
                    FeatureSet unused;
                    applyFeature(optionName, optionValue, unused, kTargetEnumValue, at);
                } while (accept(","));
                expect("]");
            }
            expect(";");
            schema.enums[(size_t)enumIndex].values.push_back(value);
        }
        if (schema.enums[(size_t)enumIndex].values.empty()) {
            Error("protobuf:parseError:enumContainLeastValue",
                "an enum must contain at least one value.", at(nameToken));
        }
        return enumIndex;
    }
    //=============================================================================
    int32_t
    Parser::parseService(const std::string& scope)
    {
        const Token& nameToken = expectIdentifier("a service name");
        ServiceDef service;
        service.file = fileIndex;
        service.name = nameToken.text;
        service.fullName = joinName(scope, service.name);
        expect("{");
        while (!accept("}")) {
            if (atEnd()) {
                Error("protobuf:parseError:unexpectedEndFileInsideService",
                    "unexpected end of file inside a service.", at(peek()));
            }
            if (accept(";")) {
                continue;
            }
            if (isIdentifier("option")) {
                next();
                parseOptionStatement();
                continue;
            }
            expectKeyword("rpc");
            MethodDef method;
            method.name = expectIdentifier("a method name").text;
            expect("(");
            if (isIdentifier("stream") && !isSymbol(")", 1)) {
                next();
                method.clientStreaming = true;
            }
            method.inputType = parseTypeName();
            expect(")");
            expectKeyword("returns");
            expect("(");
            if (isIdentifier("stream") && !isSymbol(")", 1)) {
                next();
                method.serverStreaming = true;
            }
            method.outputType = parseTypeName();
            expect(")");
            if (accept("{")) {
                while (!accept("}")) {
                    if (atEnd()) {
                        Error("protobuf:parseError:unexpectedEndFileInsideMethod",
                            "unexpected end of file inside a method.", at(peek()));
                    }
                    if (accept(";")) {
                        continue;
                    }
                    expectKeyword("option");
                    parseOptionStatement();
                }
            } else {
                expect(";");
            }
            service.methods.push_back(method);
        }
        int32_t serviceIndex = (int32_t)schema.services.size();
        schema.services.push_back(service);
        return serviceIndex;
    }
    //=============================================================================
    void
    Parser::addSyntheticOneofs(int32_t messageIndex)
    {
        // Same naming rule as the reference compiler: "_name", then "X" prefixes on clash.
        MessageDef& message = schema.messages[(size_t)messageIndex];
        std::unordered_set<std::string> names;
        for (const FieldDef& f : message.fields) {
            names.insert(f.name);
        }
        for (const OneofDef& o : message.oneofs) {
            names.insert(o.name);
        }
        for (size_t k = 0; k < message.fields.size(); ++k) {
            FieldDef& field = message.fields[k];
            if (!field.proto3Optional) {
                continue;
            }
            std::string oneofName = field.name;
            if (oneofName.empty() || oneofName[0] != '_') {
                oneofName = "_" + oneofName;
            }
            while (names.count(oneofName) > 0) {
                oneofName = "X" + oneofName;
            }
            names.insert(oneofName);
            field.oneofIndex = (int32_t)message.oneofs.size();
            OneofDef oneof;
            oneof.name = oneofName;
            oneof.synthetic = true;
            oneof.fields.push_back((int32_t)k);
            message.oneofs.push_back(oneof);
        }
    }
    //=============================================================================
    void
    Resolver::addSymbol(const std::string& name, SymbolKind kind, int line, int column)
    {
        auto it = symbols.find(name);
        if (it != symbols.end()) {
            if (kind == SymbolKind::Package && it->second == SymbolKind::Package) {
                return;
            }
            Error("protobuf:parseError:alreadyDefined", "\"%s\" is already defined.",
                Location { fileName, line, column }, name);
        }
        symbols[name] = kind;
    }
    //=============================================================================
    void
    Resolver::addPackage(const std::string& package)
    {
        size_t dot = 0;
        while (true) {
            dot = package.find('.', dot);
            addSymbol(package.substr(0, dot), SymbolKind::Package, 1, 1);
            if (dot == std::string::npos) {
                break;
            }
            dot++;
        }
    }
    //=============================================================================
    std::string
    Resolver::lookupType(
        const std::string& typeName, const std::string& relativeTo, int line, int column)
    {
        // Inner-to-outer scope search, same rule as the reference compiler.
        if (!typeName.empty() && typeName[0] == '.') {
            std::string full = typeName.substr(1);
            if (symbols.count(full) == 0) {
                Error("protobuf:parseError:notDefined", "\"%s\" is not defined.",
                    Location { fileName, line, column }, typeName);
            }
            return full;
        }
        std::string firstPart = typeName.substr(0, typeName.find('.'));
        std::string scope = relativeTo;
        while (true) {
            size_t dot = scope.find_last_of('.');
            if (dot == std::string::npos) {
                if (symbols.count(typeName) == 0) {
                    Error("protobuf:parseError:notDefined", "\"%s\" is not defined.",
                        Location { fileName, line, column }, typeName);
                }
                return typeName;
            }
            scope.erase(dot);
            std::string candidate = scope + "." + firstPart;
            auto it = symbols.find(candidate);
            if (it == symbols.end()) {
                continue;
            }
            SymbolKind kind = it->second;
            if (firstPart.size() < typeName.size()) {
                bool aggregate = kind == SymbolKind::Message || kind == SymbolKind::Package
                    || kind == SymbolKind::Enum || kind == SymbolKind::Service;
                if (aggregate) {
                    std::string full = scope + "." + typeName;
                    if (symbols.count(full) == 0) {
                        Error("protobuf:parseError:resolvedWhichDefinedUseLeading",
                            "\"%s\" is resolved to \"%s\", which is not defined; use a leading dot "
                            "to start from the outermost scope.",
                            Location { fileName, line, column }, typeName, full);
                    }
                    return full;
                }
            } else if (kind == SymbolKind::Message || kind == SymbolKind::Enum) {
                return candidate;
            }
        }
    }
    //=============================================================================
    void
    Resolver::run()
    {
        for (const FileDef& file : schema.files) {
            if (!file.package.empty()) {
                fileName = file.path;
                addPackage(file.package);
            }
        }
        for (MessageDef& m : schema.messages) {
            enterFile(m.file);
            addSymbol(m.fullName, SymbolKind::Message, m.line, m.column);
            schema.messageByName[m.fullName] = (int32_t)(&m - schema.messages.data());
        }
        for (EnumDef& e : schema.enums) {
            enterFile(e.file);
            addSymbol(e.fullName, SymbolKind::Enum, e.line, e.column);
            schema.enumByName[e.fullName] = (int32_t)(&e - schema.enums.data());
        }
        for (const ServiceDef& s : schema.services) {
            enterFile(s.file);
            addSymbol(s.fullName, SymbolKind::Service, 1, 1);
            for (const MethodDef& method : s.methods) {
                addSymbol(s.fullName + "." + method.name, SymbolKind::Method, 1, 1);
            }
        }
        for (MessageDef& m : schema.messages) {
            enterFile(m.file);
            for (const FieldDef& f : m.fields) {
                addSymbol(m.fullName + "." + f.name, SymbolKind::Field, f.line, f.column);
            }
            for (const OneofDef& o : m.oneofs) {
                addSymbol(m.fullName + "." + o.name, SymbolKind::Oneof, m.line, m.column);
            }
        }
        for (const ExtensionDef& x : schema.extensions) {
            enterFile(x.file);
            addSymbol(x.fullName, SymbolKind::Field, x.field.line, x.field.column);
        }
        for (const EnumDef& e : schema.enums) {
            enterFile(e.file);
            // Enum values are siblings of their enum type.
            size_t dot = e.fullName.find_last_of('.');
            std::string scope = dot == std::string::npos ? "" : e.fullName.substr(0, dot);
            for (const EnumValueDef& v : e.values) {
                addSymbol(joinName(scope, v.name), SymbolKind::EnumValue, e.line, e.column);
            }
        }
        for (MessageDef& m : schema.messages) {
            enterFile(m.file);
            for (FieldDef& f : m.fields) {
                resolveField(m.file, m.fullName + "." + f.name, f);
            }
        }
        for (ServiceDef& s : schema.services) {
            enterFile(s.file);
            for (MethodDef& method : s.methods) {
                std::string relativeTo = s.fullName + "." + method.name;
                method.inputType = "." + lookupType(method.inputType, relativeTo, 1, 1);
                method.outputType = "." + lookupType(method.outputType, relativeTo, 1, 1);
                if (schema.findMessage(method.inputType) < 0
                    || schema.findMessage(method.outputType) < 0) {
                    Error("protobuf:parseError:methodUseMessageTypes",
                        "method \"%s\" must use message types.", Location { fileName, 1, 1 },
                        method.name);
                }
            }
        }
        for (const FileDef& file : schema.files) {
            const FeatureSet base = file.features.over(editionDefaults(featureKey(file)));
            for (int32_t m : file.messages) {
                resolveMessageFeatures(m, base);
            }
            for (int32_t e : file.enums) {
                EnumDef& enumDef = schema.enums[(size_t)e];
                enumDef.closed = enumDef.features.over(base).enumType == 2;
            }
        }
        for (MessageDef& m : schema.messages) {
            enterFile(m.file);
            for (FieldDef& f : m.fields) {
                finalizeField(m, f);
            }
            validateMessage(m);
        }
        resolveExtensions();
        for (size_t k = 0; checkNaming && k < schema.files.size(); ++k) {
            fileName = schema.files[k].path;
            checkNamingStyle(schema.files[k]);
        }
        for (const EnumDef& e : schema.enums) {
            enterFile(e.file);
            validateEnum(e);
        }
    }
    //=============================================================================
    void
    Resolver::resolveField(int32_t file, const std::string& relativeTo, FieldDef& field)
    {
        if (field.isMap) {
            field.typeName = "." + schema.messages[(size_t)field.messageIndex].fullName;
            return;
        }
        if (field.type != FieldType::Message || field.typeName.empty()) {
            return;
        }
        std::string full = lookupType(field.typeName, relativeTo, field.line, field.column);
        auto messageIt = schema.messageByName.find(full);
        if (messageIt != schema.messageByName.end()) {
            field.type = FieldType::Message;
            field.messageIndex = messageIt->second;
        } else {
            auto enumIt = schema.enumByName.find(full);
            if (enumIt == schema.enumByName.end()) {
                Error("protobuf:parseError:notAType", "\"%s\" is not a type.",
                    Location { fileName, field.line, field.column }, field.typeName);
            }
            field.type = FieldType::Enum;
            field.enumIndex = enumIt->second;
        }
        checkVisible(file, full, field.line, field.column);
        field.typeName = "." + full;
    }
    //=============================================================================
    void
    Resolver::resolveExtensions()
    {
        // Numbers used per extended message, for duplicates.
        std::map<std::pair<int32_t, int32_t>, std::string> used;
        // Imported files first (they are loaded after the files importing them).
        std::vector<size_t> order;
        for (size_t f = schema.files.size(); f-- > 0;) {
            for (size_t k = 0; k < schema.extensions.size(); ++k) {
                if (schema.extensions[k].file == (int32_t)f) {
                    order.push_back(k);
                }
            }
        }
        for (size_t index : order) {
            ExtensionDef& x = schema.extensions[index];
            enterFile(x.file);
            FieldDef& field = x.field;
            const FileDef& file = schema.files[(size_t)x.file];
            const std::string target
                = lookupType(field.extendee, x.fullName, field.line, field.column);
            const int32_t extendee = schema.findMessage(target);
            if (extendee < 0) {
                Error("protobuf:parseError:notAMessageType", "\"%s\" is not a message type.",
                    Location { fileName, field.line, field.column }, field.extendee);
            }
            checkVisible(x.file, target, field.line, field.column);
            field.extendee = "." + target;
            resolveField(x.file, x.fullName, field);
            // Features in effect where the extension is declared.
            MessageDef context;
            context.file = x.file;
            context.resolved = x.scope >= 0 ? schema.messages[(size_t)x.scope].resolved
                                            : file.features.over(editionDefaults(featureKey(file)));
            if (field.hasJsonNameOption) {
                Error("protobuf:parseError:jsonNameAllowedExtensions",
                    "json_name is not allowed on extensions.",
                    Location { fileName, field.line, field.column });
            }
            finalizeField(context, field);
            const bool repeated = field.label == FieldLabel::Repeated;
            field.hasPresence = !repeated;
            if (field.required) {
                Error("protobuf:parseError:extensionRequired",
                    "extension \"%s\" cannot be required.",
                    Location { fileName, field.line, field.column }, x.fullName);
            }
            MessageDef& message = schema.messages[(size_t)extendee];
            if (field.number < 1 || field.number > kMaxFieldNumber) {
                Error("protobuf:parseError:fieldNumbersBetween",
                    "field numbers must be between 1 and %d.",
                    Location { fileName, field.line, field.column }, kMaxFieldNumber);
            }
            if (field.number >= kFirstImplementationNumber
                && field.number <= kLastImplementationNumber) {
                Error("protobuf:parseError:fieldNumberReservedForImplementation",
                    "field numbers 19000 through 19999 are reserved for the implementation.",
                    Location { fileName, field.line, field.column });
            }
            bool declared = false;
            for (const ReservedRange& r : message.extensionRanges) {
                declared = declared || (field.number >= r.start && field.number < r.end);
            }
            if (!declared) {
                Error("protobuf:parseError:doesDeclareExtensionNumber",
                    "\"%s\" does not declare %d as an extension number.",
                    Location { fileName, field.line, field.column }, message.fullName,
                    field.number);
            }
            if (file.syntax == "proto3") {
                const std::string prefix = "google.protobuf.";
                const std::string suffix = "Options";
                const bool options = message.fullName.compare(0, prefix.size(), prefix) == 0
                    && message.fullName.size() > suffix.size()
                    && message.fullName.compare(
                           message.fullName.size() - suffix.size(), suffix.size(), suffix)
                        == 0;
                if (!options) {
                    Error("protobuf:parseError:extensionsProto3AllowedDefiningOptions",
                        "extensions in proto3 are only allowed for defining options.",
                        Location { fileName, field.line, field.column });
                }
            }
            auto it = used.find({ extendee, field.number });
            if (it != used.end()) {
                Error("protobuf:parseError:extensionNumberAlreadyUsed",
                    "extension number %d of \"%s\" is already used by \"%s\".",
                    Location { fileName, field.line, field.column }, field.number, message.fullName,
                    it->second);
            }
            used[{ extendee, field.number }] = x.fullName;
            // The copy in the extended message: the short name as member name, the full name
            // with underscores when the short one is taken.
            FieldDef member = field;
            member.extension = x.fullName;
            member.oneofIndex = -1;
            if (message.findFieldByName(member.name) >= 0) {
                member.name = x.fullName;
                std::replace(member.name.begin(), member.name.end(), '.', '_');
            }
            message.fields.push_back(member);
            buildLookup(message);
        }
    }
    //=============================================================================
    void
    Resolver::checkVisible(int32_t fromFile, const std::string& fullName, int line, int column)
    {
        auto messageIt = schema.messageByName.find(fullName);
        int32_t file = -1;
        Visibility visibility = Visibility::Unset;
        if (messageIt != schema.messageByName.end()) {
            const MessageDef& m = schema.messages[(size_t)messageIt->second];
            file = m.file;
            visibility = m.visibility;
        } else {
            auto enumIt = schema.enumByName.find(fullName);
            if (enumIt == schema.enumByName.end()) {
                return;
            }
            file = schema.enums[(size_t)enumIt->second].file;
            visibility = schema.enums[(size_t)enumIt->second].visibility;
        }
        if (file == fromFile || visibility == Visibility::Export) {
            return;
        }
        const FileDef& owner = schema.files[(size_t)file];
        bool visible = visibility != Visibility::Local;
        if (visible) {
            const int8_t rule
                = owner.features.over(editionDefaults(featureKey(owner))).defaultSymbolVisibility;
            const std::string local
                = owner.package.empty() ? fullName : fullName.substr(owner.package.size() + 1);
            const bool topLevel = local.find('.') == std::string::npos;
            visible = rule == 1 || (rule == 2 && topLevel);
        }
        if (!visible) {
            Error("protobuf:parseError:localUsedAnotherFile",
                "\"%s\" is local to \"%s\" and cannot be used from another file.",
                Location { fileName, line, column }, fullName, owner.name);
        }
    }
    //=============================================================================
    void
    Resolver::resolveMessageFeatures(int32_t messageIndex, const FeatureSet& parent)
    {
        MessageDef& message = schema.messages[(size_t)messageIndex];
        message.resolved = message.features.over(parent);
        const FeatureSet resolved = message.resolved;
        const std::vector<int32_t> nestedMessages = message.nestedMessages;
        for (int32_t nested : nestedMessages) {
            FeatureSet base = resolved;
            if (schema.messages[(size_t)nested].mapEntry) {
                // The features of a map field apply to its key and value.
                for (const FieldDef& f : schema.messages[(size_t)messageIndex].fields) {
                    if (f.isMap && f.messageIndex == nested) {
                        base = f.features.over(resolved);
                    }
                }
            }
            resolveMessageFeatures(nested, base);
        }
        for (int32_t nested : schema.messages[(size_t)messageIndex].nestedEnums) {
            EnumDef& enumDef = schema.enums[(size_t)nested];
            enumDef.closed = enumDef.features.over(resolved).enumType == 2;
        }
    }
    //=============================================================================
    void
    Resolver::checkNamingStyle(const FileDef& file)
    {
        const FeatureSet fileFeatures = file.features.over(editionDefaults(featureKey(file)));
        auto check = [this](const std::string& kind, const std::string& name, NamingRule rule,
                         int line, int column) {
            const Location where { fileName, line, column };
            switch (rule) {
            case NamingRule::Ok:
                return;
            case NamingRule::TitleCase:
                Error("protobuf:parseError:nameNotTitleCase",
                    "%s name \"%s\" should be TitleCase (features.enforce_naming_style = "
                    "STYLE_LEGACY turns this check off).",
                    where, kind, name);
            case NamingRule::CapitalFirst:
                Error("protobuf:parseError:nameNotCapitalFirst",
                    "%s name \"%s\" should begin with a capital letter "
                    "(features.enforce_naming_style = STYLE_LEGACY turns this check off).",
                    where, kind, name);
            case NamingRule::LowerFirst:
                Error("protobuf:parseError:nameNotLowerFirst",
                    "%s name \"%s\" should begin with a lower case letter "
                    "(features.enforce_naming_style = STYLE_LEGACY turns this check off).",
                    where, kind, name);
            case NamingRule::LowerSnake:
                Error("protobuf:parseError:nameNotLowerSnake",
                    "%s name \"%s\" should be lower_snake_case (features.enforce_naming_style = "
                    "STYLE_LEGACY turns this check off).",
                    where, kind, name);
            case NamingRule::UpperSnake:
                Error("protobuf:parseError:nameNotUpperSnake",
                    "%s name \"%s\" should be UPPER_SNAKE_CASE (features.enforce_naming_style = "
                    "STYLE_LEGACY turns this check off).",
                    where, kind, name);
            case NamingRule::Underscores:
                Error("protobuf:parseError:nameBadUnderscores",
                    "%s name \"%s\" contains underscores that break the style "
                    "(features.enforce_naming_style = STYLE_LEGACY turns this check off).",
                    where, kind, name);
            }
        };
        if (fileFeatures.enforceNamingStyle == 1 && !file.package.empty()) {
            size_t start = 0;
            while (true) {
                const size_t dot = file.package.find('.', start);
                const std::string part = file.package.substr(start, dot - start);
                if (lowerSnakeError(part) != NamingRule::Ok) {
                    check("package", file.package, NamingRule::LowerSnake, 1, 1);
                }
                if (dot == std::string::npos) {
                    break;
                }
                start = dot + 1;
            }
        }
        const int32_t fileIndex = (int32_t)(&file - schema.files.data());
        for (const MessageDef& m : schema.messages) {
            if (m.file != fileIndex || m.mapEntry) {
                continue;
            }
            if (m.resolved.enforceNamingStyle == 1) {
                check("message", m.name, titleCaseError(m.name), m.line, m.column);
                for (const OneofDef& o : m.oneofs) {
                    if (o.features.over(m.resolved).enforceNamingStyle == 1) {
                        check("oneof", o.name, lowerSnakeError(o.name), m.line, m.column);
                    }
                }
            }
            for (const FieldDef& f : m.fields) {
                if (f.features.over(m.resolved).enforceNamingStyle == 1) {
                    check("field", f.name, lowerSnakeError(f.name), f.line, f.column);
                }
            }
        }
        for (const EnumDef& e : schema.enums) {
            if (e.file != fileIndex) {
                continue;
            }
            // Nested enums follow the style in effect where they are written.
            FeatureSet scope = fileFeatures;
            for (const MessageDef& m : schema.messages) {
                for (int32_t nested : m.nestedEnums) {
                    if (&schema.enums[(size_t)nested] == &e) {
                        scope = m.resolved;
                    }
                }
            }
            if (e.features.over(scope).enforceNamingStyle == 1) {
                check("enum", e.name, titleCaseError(e.name), e.line, e.column);
                for (const EnumValueDef& v : e.values) {
                    check("enum value", v.name, upperSnakeError(v.name), e.line, e.column);
                }
            }
        }
        if (fileFeatures.enforceNamingStyle == 1) {
            for (const ServiceDef& service : schema.services) {
                if (service.file != fileIndex) {
                    continue;
                }
                check("service", service.name, titleCaseError(service.name), 1, 1);
                for (const MethodDef& method : service.methods) {
                    check("method", method.name, titleCaseError(method.name), 1, 1);
                }
            }
        }
    }
    //=============================================================================
    void
    Resolver::finalizeField(MessageDef& message, FieldDef& field)
    {
        field.kind = valueKindOf(field.type);
        bool repeated = field.label == FieldLabel::Repeated;
        // Like the reference compiler, only packed = true is checked.
        if (field.hasPackedOption && field.packedOption
            && (!repeated || !isPackableType(field.type))) {
            Error("protobuf:parseError:packedTrueUsedRepeatedScalar",
                "packed = true can only be used on repeated scalar numeric fields.",
                Location { fileName, field.line, field.column });
        }
        bool realOneof
            = field.oneofIndex >= 0 && !message.oneofs[(size_t)field.oneofIndex].synthetic;
        // proto2 and editions files: wire behaviour from the features (proto2 ones are fixed).
        const bool editionsFile = schema.files[(size_t)message.file].syntax != "proto3";
        const FeatureSet features = field.features.over(field.oneofIndex >= 0
                ? message.oneofs[(size_t)field.oneofIndex].features.over(message.resolved)
                : message.resolved);
        if (editionsFile) {
            const FeatureSet& written = field.features;
            if (written.fieldPresence != 0 && repeated) {
                Error("protobuf:parseError:repeatedFieldsSetFieldPresence",
                    "repeated fields cannot set field presence.",
                    Location { fileName, field.line, field.column });
            }
            if (written.fieldPresence != 0 && realOneof) {
                Error("protobuf:parseError:oneofFieldsSetFieldPresence",
                    "oneof fields cannot set field presence.",
                    Location { fileName, field.line, field.column });
            }
            if (written.fieldPresence == 2 && field.type == FieldType::Message) {
                Error("protobuf:parseError:messageFieldsImplicitPresence",
                    "message fields cannot have implicit presence.",
                    Location { fileName, field.line, field.column });
            }
            if (written.repeatedFieldEncoding != 0 && !repeated) {
                Error("protobuf:parseError:repeatedFieldsSetRepeatedField",
                    "only repeated fields can set the repeated field encoding.",
                    Location { fileName, field.line, field.column });
            }
            if (written.messageEncoding != 0 && (field.type != FieldType::Message || field.isMap)) {
                Error("protobuf:parseError:messageFieldsSetMessageEncoding",
                    "only message fields can set the message encoding.",
                    Location { fileName, field.line, field.column });
            }
            if (written.utf8Validation != 0 && field.type != FieldType::String && !field.isMap) {
                Error("protobuf:parseError:stringFieldsSetUTFValidation",
                    "only string fields can set UTF-8 validation.",
                    Location { fileName, field.line, field.column });
            }
            field.packed = repeated && isPackableType(field.type)
                && (field.hasPackedOption ? field.packedOption
                                          : features.repeatedFieldEncoding == 1);
            field.hasPresence = !repeated
                && (realOneof || field.type == FieldType::Message || features.fieldPresence != 2);
            field.required = !repeated && !realOneof
                && (features.fieldPresence == 3 || field.label == FieldLabel::Required);
            field.delimited = field.isGroup
                || (field.type == FieldType::Message && !field.isMap && !message.mapEntry
                    && features.messageEncoding == 2);
            field.utf8Check = features.utf8Validation != 3;
        } else {
            field.packed = repeated && isPackableType(field.type)
                && (field.hasPackedOption ? field.packedOption : true);
            field.hasPresence = !repeated
                && (field.proto3Optional || realOneof || field.type == FieldType::Message);
        }
        if (field.type == FieldType::Enum && !repeated && !field.hasPresence
            && schema.enums[(size_t)field.enumIndex].closed) {
            Error("protobuf:parseError:fieldsImplicitPresenceUseOpen",
                "fields with implicit presence must use open enums.",
                Location { fileName, field.line, field.column });
        }
        if (field.hasDefault) {
            if (!field.hasPresence) {
                Error("protobuf:parseError:fieldsImplicitPresenceDefaultValues",
                    "fields with implicit presence cannot have default values.",
                    Location { fileName, field.line, field.column });
            }
            if (field.type == FieldType::Message) {
                Error("protobuf:parseError:messageFieldsDefaultValues",
                    "message fields cannot have default values.",
                    Location { fileName, field.line, field.column });
            }
            if (field.type == FieldType::Enum) {
                const EnumDef& enumDef = schema.enums[(size_t)field.enumIndex];
                bool found = false;
                for (const EnumValueDef& v : enumDef.values) {
                    found = found || v.name == field.defaultValue;
                }
                if (!found) {
                    Error("protobuf:parseError:notAnEnumValue",
                        "\"%s\" is not a value of enum \"%s\".",
                        Location { fileName, field.line, field.column }, field.defaultValue,
                        enumDef.name);
                }
            }
        }
        if (!field.hasJsonNameOption) {
            field.jsonName = toJsonName(field.name);
        }
    }
    //=============================================================================
    void
    Resolver::validateMessage(MessageDef& message)
    {
        std::map<int32_t, const FieldDef*> numbers;
        std::map<std::string, const FieldDef*> jsonNames;
        for (const FieldDef& f : message.fields) {
            if (f.number < 1) {
                Error("protobuf:parseError:fieldNumbersPositiveIntegers",
                    "field numbers must be positive integers.",
                    Location { fileName, f.line, f.column });
            }
            if (f.number > kMaxFieldNumber) {
                Error("protobuf:parseError:fieldNumbersGreater",
                    "field numbers cannot be greater than %d.",
                    Location { fileName, f.line, f.column }, kMaxFieldNumber);
            }
            if (f.number >= kFirstImplementationNumber && f.number <= kLastImplementationNumber) {
                Error("protobuf:parseError:fieldNumberReservedForImplementation",
                    "field numbers 19000 through 19999 are reserved for the implementation.",
                    Location { fileName, f.line, f.column });
            }
            auto it = numbers.find(f.number);
            if (it != numbers.end()) {
                Error("protobuf:parseError:fieldNumberAlreadyUsedField",
                    "field number %d is already used in \"%s\" by field \"%s\".",
                    Location { fileName, f.line, f.column }, f.number, message.fullName,
                    it->second->name);
            }
            numbers[f.number] = &f;
            for (const ReservedRange& r : message.reservedRanges) {
                if (f.number >= r.start && f.number < r.end) {
                    Error("protobuf:parseError:fieldUsesReservedNumber",
                        "field \"%s\" uses reserved number %d.",
                        Location { fileName, f.line, f.column }, f.name, f.number);
                }
            }
            for (const std::string& reservedName : message.reservedNames) {
                if (f.name == reservedName) {
                    Error("protobuf:parseError:fieldNameReserved", "field name \"%s\" is reserved.",
                        Location { fileName, f.line, f.column }, f.name);
                }
            }
            if (!message.mapEntry && message.resolved.jsonFormat != 2
                && !message.legacyJsonConflicts) {
                auto jsonIt = jsonNames.find(f.jsonName);
                if (jsonIt != jsonNames.end()) {
                    Error("protobuf:parseError:jsonNameFieldConflictsField",
                        "the JSON name of field \"%s\" conflicts with field \"%s\".",
                        Location { fileName, f.line, f.column }, f.name, jsonIt->second->name);
                }
                jsonNames[f.jsonName] = &f;
            }
        }
        for (size_t k = 0; k < message.extensionRanges.size(); ++k) {
            const ReservedRange& r = message.extensionRanges[k];
            for (const FieldDef& f : message.fields) {
                if (f.number >= r.start && f.number < r.end) {
                    Error("protobuf:parseError:extensionRangeIncludesField",
                        "extension range %d to %d includes field \"%s\" (%d).",
                        Location { fileName, f.line, f.column }, r.start, r.end - 1, f.name,
                        f.number);
                }
            }
            for (const ReservedRange& reserved : message.reservedRanges) {
                if (r.start < reserved.end && reserved.start < r.end) {
                    Error("protobuf:parseError:extensionRangeOverlapsReservedRange",
                        "extension range %d to %d overlaps a reserved range.",
                        Location { fileName, message.line, message.column }, r.start, r.end - 1);
                }
            }
            for (size_t j = 0; j < k; ++j) {
                const ReservedRange& other = message.extensionRanges[j];
                if (r.start < other.end && other.start < r.end) {
                    Error("protobuf:parseError:extensionRangesOverlap",
                        "extension range %d to %d overlaps another extension range.",
                        Location { fileName, message.line, message.column }, r.start, r.end - 1);
                }
            }
        }
        buildLookup(message);
    }
    //=============================================================================
    void
    Resolver::buildLookup(MessageDef& message)
    {
        int32_t maxNumber = 0;
        for (const FieldDef& f : message.fields) {
            maxNumber = std::max(maxNumber, f.number);
        }
        message.denseLookup.clear();
        message.sparseLookup.clear();
        if (maxNumber <= 8192) {
            message.denseLookup.assign((size_t)maxNumber + 1, -1);
            for (size_t k = 0; k < message.fields.size(); ++k) {
                message.denseLookup[(size_t)message.fields[k].number] = (int32_t)k;
            }
        } else {
            for (size_t k = 0; k < message.fields.size(); ++k) {
                message.sparseLookup[message.fields[k].number] = (int32_t)k;
            }
        }
    }
    //=============================================================================
    void
    Resolver::validateEnum(const EnumDef& enumDef)
    {
        if (enumDef.values[0].number != 0 && !enumDef.closed) {
            Error("protobuf:parseError:firstValueOpenEnumZero",
                "the first value of open enum \"%s\" must be zero.",
                Location { fileName, enumDef.line, enumDef.column }, enumDef.name);
        }
        std::map<int32_t, std::string> numbers;
        bool hasAlias = false;
        for (const EnumValueDef& v : enumDef.values) {
            auto it = numbers.find(v.number);
            if (it != numbers.end()) {
                if (!enumDef.allowAlias) {
                    Error("protobuf:parseError:usesSameValueSetOption",
                        "\"%s\" uses the same value as \"%s\"; set option allow_alias = true to "
                        "allow it.",
                        Location { fileName, enumDef.line, enumDef.column }, v.name, it->second);
                }
                hasAlias = true;
            } else {
                numbers[v.number] = v.name;
            }
            for (const ReservedRange& r : enumDef.reservedRanges) {
                if (v.number >= r.start && v.number <= r.end) {
                    Error("protobuf:parseError:enumValueUsesReservedNumber",
                        "enum value \"%s\" uses a reserved number.",
                        Location { fileName, enumDef.line, enumDef.column }, v.name);
                }
            }
            for (const std::string& reservedName : enumDef.reservedNames) {
                if (v.name == reservedName) {
                    Error("protobuf:parseError:enumValueNameReserved",
                        "enum value name \"%s\" is reserved.",
                        Location { fileName, enumDef.line, enumDef.column }, v.name);
                }
            }
        }
        if (enumDef.allowAlias && !hasAlias) {
            Error("protobuf:parseError:enumSetsAllowAliasBut",
                "enum \"%s\" sets allow_alias but has no aliases.",
                Location { fileName, enumDef.line, enumDef.column }, enumDef.name);
        }
    }
    //=============================================================================
    std::string
    textFormatString(const std::string& text)
    {
        std::string out = "\"";
        for (unsigned char c : text) {
            switch (c) {
            case '\\':
                out += "\\\\";
                break;
            case '"':
                out += "\\\"";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    const char digits[] = { '\\', (char)('0' + (c >> 6)),
                        (char)('0' + ((c >> 3) & 7)), (char)('0' + (c & 7)), 0 };
                    out += digits;
                } else {
                    out += (char)c;
                }
            }
        }
        return out + "\"";
    }
    //=============================================================================
    std::string
    joinName(const std::string& scope, const std::string& name)
    {
        return scope.empty() ? name : scope + "." + name;
    }
    //=============================================================================
    std::string
    toJsonName(const std::string& name)
    {
        std::string result;
        bool capitalizeNext = false;
        for (char c : name) {
            if (c == '_') {
                capitalizeNext = true;
            } else if (capitalizeNext) {
                result.push_back((c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c);
                capitalizeNext = false;
            } else {
                result.push_back(c);
            }
        }
        return result;
    }
    //=============================================================================
    std::string
    mapEntryName(const std::string& fieldName)
    {
        std::string result;
        bool capitalizeNext = true;
        for (char c : fieldName) {
            if (c == '_') {
                capitalizeNext = true;
            } else if (capitalizeNext) {
                result.push_back((c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c);
                capitalizeNext = false;
            } else {
                result.push_back(c);
            }
        }
        return result + "Entry";
    }
    //=============================================================================
    bool
    scalarTypeFromKeyword(const std::string& word, FieldType& type)
    {
        static const std::unordered_map<std::string, FieldType> table = {
            { "double", FieldType::Double },
            { "float", FieldType::Float },
            { "int64", FieldType::Int64 },
            { "uint64", FieldType::UInt64 },
            { "int32", FieldType::Int32 },
            { "fixed64", FieldType::Fixed64 },
            { "fixed32", FieldType::Fixed32 },
            { "bool", FieldType::Bool },
            { "string", FieldType::String },
            { "bytes", FieldType::Bytes },
            { "uint32", FieldType::UInt32 },
            { "sfixed32", FieldType::SFixed32 },
            { "sfixed64", FieldType::SFixed64 },
            { "sint32", FieldType::SInt32 },
            { "sint64", FieldType::SInt64 },
        };
        auto it = table.find(word);
        if (it == table.end()) {
            return false;
        }
        type = it->second;
        return true;
    }
    //=============================================================================
    bool
    isValidMapKeyType(FieldType type)
    {
        return type != FieldType::Double && type != FieldType::Float && type != FieldType::Bytes
            && type != FieldType::Message && type != FieldType::Enum && type != FieldType::Group;
    }
    //=============================================================================
    uint64_t
    parseIntegerText(const std::string& text, bool& overflow)
    {
        overflow = false;
        uint64_t value = 0;
        unsigned base = 10;
        size_t k = 0;
        if (text.size() > 1 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
            base = 16;
            k = 2;
        } else if (text.size() > 1 && text[0] == '0') {
            base = 8;
            k = 1;
        }
        for (; k < text.size(); ++k) {
            int digit = hexValue(text[k]);
            if (digit < 0 || (unsigned)digit >= base) {
                overflow = true;
                return 0;
            }
            if (value > (UINT64_MAX - (uint64_t)digit) / base) {
                overflow = true;
                return 0;
            }
            value = value * base + (uint64_t)digit;
        }
        return value;
    }
    //=============================================================================
    void
    appendUtf8(std::string& out, uint32_t codePoint)
    {
        if (codePoint < 0x80) {
            out.push_back((char)codePoint);
        } else if (codePoint < 0x800) {
            out.push_back((char)(0xC0 | (codePoint >> 6)));
            out.push_back((char)(0x80 | (codePoint & 0x3F)));
        } else if (codePoint < 0x10000) {
            out.push_back((char)(0xE0 | (codePoint >> 12)));
            out.push_back((char)(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (codePoint & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (codePoint >> 18)));
            out.push_back((char)(0x80 | ((codePoint >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((codePoint >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (codePoint & 0x3F)));
        }
    }
    //=============================================================================
    int
    hexValue(char c)
    {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }
        return -1;
    }
    //=============================================================================
    std::string
    featureKey(const FileDef& file)
    {
        return file.edition.empty() ? file.syntax : file.edition;
    }
    //=============================================================================
    FeatureSet
    editionDefaults(const std::string& edition)
    {
        FeatureSet features;
        if (edition == "proto2") {
            // The legacy behaviour of proto2 files.
            features.fieldPresence = 1;
            features.enumType = 2;
            features.repeatedFieldEncoding = 2;
            features.utf8Validation = 3;
            features.messageEncoding = 1;
            features.jsonFormat = 2;
            features.enforceNamingStyle = 2;
            features.defaultSymbolVisibility = 1;
            return features;
        }
        features.fieldPresence = edition.empty() || edition == "proto3" ? 2 : 1;
        features.enumType = 1;
        features.repeatedFieldEncoding = 1;
        features.utf8Validation = 2;
        features.messageEncoding = 1;
        features.jsonFormat = 1;
        features.enforceNamingStyle = edition == "2024" ? 1 : 2;
        features.defaultSymbolVisibility = edition == "2024" ? 2 : 1;
        return features;
    }
    //=============================================================================
    std::string
    simpleDouble(double value)
    {
        if (std::isinf(value)) {
            return value > 0 ? "inf" : "-inf";
        }
        if (std::isnan(value)) {
            return "nan";
        }
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.15g", value);
        if (std::strtod(buffer, nullptr) != value) {
            std::snprintf(buffer, sizeof(buffer), "%.17g", value);
        }
        return buffer;
    }
    //=============================================================================
    std::string
    simpleFloat(float value)
    {
        if (std::isinf(value)) {
            return value > 0 ? "inf" : "-inf";
        }
        if (std::isnan(value)) {
            return "nan";
        }
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.6g", (double)value);
        if (std::strtof(buffer, nullptr) != value) {
            std::snprintf(buffer, sizeof(buffer), "%.9g", (double)value);
        }
        return buffer;
    }
    //=============================================================================
    std::string
    cEscape(const std::string& bytes)
    {
        std::string out;
        for (unsigned char c : bytes) {
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
            case '\'':
                out += "\\'";
                break;
            case '\\':
                out += "\\\\";
                break;
            default:
                if (c < 0x20 || c >= 0x7f) {
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
    NamingRule
    titleCaseError(const std::string& name)
    {
        if (name.find('_') != std::string::npos) {
            return NamingRule::TitleCase;
        }
        if (name.empty() || !std::isupper((unsigned char)name[0])) {
            return NamingRule::CapitalFirst;
        }
        return NamingRule::Ok;
    }
    //=============================================================================
    NamingRule
    snakeError(const std::string& name, bool upper)
    {
        for (unsigned char c : name) {
            if (upper ? std::islower(c) : std::isupper(c)) {
                return upper ? NamingRule::UpperSnake : NamingRule::LowerSnake;
            }
        }
        if (name.empty() || !std::isalpha((unsigned char)name[0])) {
            return upper ? NamingRule::CapitalFirst : NamingRule::LowerFirst;
        }
        for (size_t k = 0; k < name.size(); ++k) {
            if (name[k] == '_'
                && (k + 1 == name.size() || !std::isalpha((unsigned char)name[k + 1]))) {
                return NamingRule::Underscores;
            }
        }
        return NamingRule::Ok;
    }
    //=============================================================================
    NamingRule
    lowerSnakeError(const std::string& name)
    {
        return snakeError(name, false);
    }
    //=============================================================================
    NamingRule
    upperSnakeError(const std::string& name)
    {
        return snakeError(name, true);
    }
    //=============================================================================
    //=============================================================================
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
