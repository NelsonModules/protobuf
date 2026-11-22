//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtobufError.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
//=============================================================================
void
Nelson::ProtobufGateway::raiseProtoError(
    const Protobuf::ProtoError& e, const std::string& fallbackId)
{
    const std::string message
        = e.render([](const std::string& id, const std::string& messageTemplate) {
              return resolveMessageTemplate(id, messageTemplate);
          });
    const std::string& id = e.innerId();
    RaiseError(message, id.empty() ? fallbackId : id);
    throw std::logic_error(message);
}
//=============================================================================
