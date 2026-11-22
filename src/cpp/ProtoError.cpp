//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtoError.hpp"
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
namespace {
    std::string
    english(const std::string&, const std::string& messageTemplate);
} // namespace
//=============================================================================
ProtoError::ProtoError(const std::string& message)
    : std::runtime_error(message), messageTemplate(message)
{
}
//=============================================================================
ProtoError::ProtoError(
    std::string id, std::string messageTemplate, std::vector<Argument> arguments, Location location)
    : std::runtime_error("")
    , identifier(std::move(id))
    , messageTemplate(std::move(messageTemplate))
    , arguments(std::move(arguments))
    , location(std::move(location))
{
    static_cast<std::runtime_error&>(*this) = std::runtime_error(render(english));
}
//=============================================================================
const std::string&
ProtoError::innerId() const
{
    for (const Argument& argument : arguments) {
        if (argument.error && !argument.error->innerId().empty()) {
            return argument.error->innerId();
        }
    }
    return identifier;
}
//=============================================================================
std::string
ProtoError::render(
    const std::function<std::string(const std::string&, const std::string&)>& translate) const
{
    if (identifier.empty()) {
        return messageTemplate;
    }
    const std::string text = translate(identifier, messageTemplate);
    std::string out;
    if (!location.file.empty()) {
        out = location.file + ":" + std::to_string(location.line) + ":"
            + std::to_string(location.column) + ": ";
    }
    size_t next = 0;
    for (size_t k = 0; k < text.size(); ++k) {
        if (text[k] == '%' && k + 1 < text.size() && (text[k + 1] == 's' || text[k + 1] == 'd')
            && next < arguments.size()) {
            const Argument& argument = arguments[next++];
            out += argument.error ? argument.error->render(translate) : argument.text;
            k++;
        } else {
            out += text[k];
        }
    }
    return out;
}
//=============================================================================
namespace {
    std::string
    english(const std::string&, const std::string& messageTemplate)
    {
        return messageTemplate;
    }
} // namespace
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
