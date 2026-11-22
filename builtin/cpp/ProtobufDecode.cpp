//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "ProtobufDecode.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "ProtoStream.hpp"
#include <cmath>
//=============================================================================
namespace Nelson::ProtobufGateway {
//=============================================================================
ArrayOfVector
decodeData(int nLhs, const SchemaEntry& entry, const std::string& typeName, const uint8_t* data,
    size_t size, const DecodeSettings& settings, const Protobuf::RecordIndex* index)
{
    const Protobuf::Codec& codec = *entry.codec;
    Protobuf::ColumnSet set;
    Protobuf::StreamResult result;
    try {
        int32_t messageIndex = entry.schema.resolveMessage(typeName);
        set = codec.createColumnSet(messageIndex);
        codec.select(set, fieldSelection(entry.schema, messageIndex, settings.fields));
        set.keepUnknown = settings.keepUnknown;
        size_t firstRecord = 0;
        if (!settings.records.isEmpty()) {
            // Only the bytes of records first to last are decoded.
            ArrayOf range = settings.records;
            range.promoteType(NLS_DOUBLE);
            const double* bounds = static_cast<const double*>(range.dataPointer());
            firstRecord = (size_t)bounds[0] - 1;
            const size_t begin = index != nullptr
                ? Protobuf::indexedRecordOffset(*index, data, size, firstRecord)
                : Protobuf::skipRecords(data, size, firstRecord);
            data += begin;
            size -= begin;
            if (std::isfinite(bounds[1])) {
                size = Protobuf::skipRecords(data, size, (size_t)(bounds[1] - bounds[0] + 1));
            }
        }
        if (settings.delimited) {
            result = Protobuf::appendDelimitedParallel(
                codec, set, data, size, settings.skipInvalid, settings.workers, firstRecord);
        } else {
            codec.appendMessage(set, data, size);
            result.records = 1;
        }
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:decodeError");
    }
    ArrayOfVector retval;
    ArrayOf presence;
    retval.push_back(rowsToValue(codec, set, settings, presence));
    if (nLhs > 1) {
        retval.push_back(presence);
    }
    if (nLhs > 2) {
        retval.push_back(decodeReport(entry.schema.messages[(size_t)set.messageIndex].fullName,
            result.records, result.skipped));
    }
    return retval;
}
//=============================================================================
} // namespace Nelson::ProtobufGateway
//=============================================================================
