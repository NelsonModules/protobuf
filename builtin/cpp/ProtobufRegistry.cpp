//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtobufRegistry.hpp"
#include "ErrorID.hpp"
#include <map>
#include <mutex>
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
namespace {
    std::mutex registryMutex;
    std::map<uint64_t, std::shared_ptr<SchemaEntry>> registry;
    std::map<uint64_t, std::shared_ptr<DecoderEntry>> decoders;
    uint64_t nextId = 1;
} // namespace
//=============================================================================
uint64_t
registerSchema(Protobuf::Schema&& schema, const std::string& path)
{
    auto entry = std::make_shared<SchemaEntry>();
    entry->schema = std::move(schema);
    entry->codec = std::make_unique<Protobuf::Codec>(entry->schema);
    entry->path = path;
    std::lock_guard<std::mutex> lock(registryMutex);
    uint64_t id = nextId++;
    registry[id] = entry;
    return id;
}
//=============================================================================
std::shared_ptr<const SchemaEntry>
requireSchema(double id)
{
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        auto it = registry.find((uint64_t)id);
        if (id >= 1 && it != registry.end()) {
            return it->second;
        }
    }
    Error(L"protobuf:invalidSchema:invalidReleasedProtobufSchema",
        L"Invalid or released protobuf schema.");
    return nullptr;
}
//=============================================================================
bool
releaseSchema(double id)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    return registry.erase((uint64_t)id) > 0;
}
//=============================================================================
void
releaseAllSchemas()
{
    std::lock_guard<std::mutex> lock(registryMutex);
    decoders.clear();
    registry.clear();
}
//=============================================================================
uint64_t
registerDecoder(std::shared_ptr<const SchemaEntry> schema, int32_t messageIndex, bool skipInvalid)
{
    auto entry = std::make_shared<DecoderEntry>();
    entry->decoder
        = std::make_unique<Protobuf::StreamDecoder>(*schema->codec, messageIndex, skipInvalid);
    entry->schema = std::move(schema);
    std::lock_guard<std::mutex> lock(registryMutex);
    uint64_t id = nextId++;
    decoders[id] = entry;
    return id;
}
//=============================================================================
std::shared_ptr<DecoderEntry>
requireDecoder(double id)
{
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        auto it = decoders.find((uint64_t)id);
        if (id >= 1 && it != decoders.end()) {
            return it->second;
        }
    }
    Error(L"protobuf:invalidDecoder:invalidReleasedProtobufDecoder",
        L"Invalid or released protobuf decoder.");
    return nullptr;
}
//=============================================================================
bool
releaseDecoder(double id)
{
    std::lock_guard<std::mutex> lock(registryMutex);
    return decoders.erase((uint64_t)id) > 0;
}
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
