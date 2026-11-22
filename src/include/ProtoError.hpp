//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
//=============================================================================
namespace Nelson::Protobuf {
//=============================================================================
// Place of a .proto error, written "file:line:column: " before the message.
struct Location
{
    std::string file;
    int line = 0;
    int column = 0;
};
//=============================================================================
// Error of the parser, the resolver and the codec: an identifier ("protobuf:parseError:...")
// and an English template whose %s/%d placeholders take the arguments in order; an argument
// can be another error (its message). what() is the English message.
class ProtoError : public std::runtime_error
{
public:
    struct Argument
    {
        std::string text;
        std::shared_ptr<const ProtoError> error;
    };
    // Message without identifier (not translated).
    explicit ProtoError(const std::string& message);
    ProtoError(std::string id, std::string messageTemplate, std::vector<Argument> arguments,
        Location location);
    const std::string&
    id() const
    {
        return identifier;
    }
    // Identifier of the innermost error.
    const std::string&
    innerId() const;
    // Message with each template replaced by translate(id, template) first.
    std::string
    render(
        const std::function<std::string(const std::string&, const std::string&)>& translate) const;

private:
    std::string identifier;
    std::string messageTemplate;
    std::vector<Argument> arguments;
    Location location;
};
//=============================================================================
namespace detail {
    inline void
    collect(std::vector<ProtoError::Argument>&, Location&)
    {
    }
    template <typename First, typename... Rest>
    void
    collect(std::vector<ProtoError::Argument>& out, Location& location, const First& first,
        const Rest&... rest)
    {
        if constexpr (std::is_same_v<First, Location>) {
            location = first;
        } else if constexpr (std::is_base_of_v<ProtoError, First>) {
            out.push_back({ "", std::make_shared<ProtoError>(first) });
        } else if constexpr (std::is_same_v<First, char>) {
            out.push_back({ std::string(1, first), nullptr });
        } else if constexpr (std::is_integral_v<First> && !std::is_same_v<First, bool>) {
            out.push_back({ std::to_string(first), nullptr });
        } else {
            out.push_back({ std::string(first), nullptr });
        }
        collect(out, location, rest...);
    }
} // namespace detail
//=============================================================================
// Error id with its English template; a Location argument sets the place, the other
// arguments fill the placeholders.
template <typename... Args>
ProtoError
Message(const char* id, const char* messageTemplate, const Args&... args)
{
    std::vector<ProtoError::Argument> arguments;
    Location location;
    detail::collect(arguments, location, args...);
    return ProtoError(id, messageTemplate, std::move(arguments), location);
}
//=============================================================================
// Throws Message(id, messageTemplate, args...).
template <typename... Args>
[[noreturn]] void
Error(const char* id, const char* messageTemplate, const Args&... args)
{
    throw Message(id, messageTemplate, args...);
}
//=============================================================================
} // namespace Nelson::Protobuf
//=============================================================================
