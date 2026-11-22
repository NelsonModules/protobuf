//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#pragma once
//=============================================================================
#include "ProtoError.hpp"
#include <string>
//=============================================================================
namespace Nelson::ProtobufGateway {
// Raises a core error in the current language: each template is looked up by its identifier in
// the module catalog; fallbackId is used when the error has no identifier.
[[noreturn]] void
raiseProtoError(const Protobuf::ProtoError& e, const std::string& fallbackId);
} // namespace Nelson::ProtobufGateway
//=============================================================================
