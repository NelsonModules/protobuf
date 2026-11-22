//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecoderReadBuiltin.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufConversion.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// [value, presence, report] = __protobuf_decoder_read__(id, options): layout options.
// Returns the rows decoded since the previous read and forgets them.
ArrayOfVector
Nelson::ProtobufGateway::protobufDecoderReadBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 2, 2);
    nargoutcheck(nLhs, 0, 3);
    std::shared_ptr<DecoderEntry> entry = requireDecoder(argIn[0].asDoubleScalar());
    const DecodeSettings settings = decodeSettings(argIn[1]);
    const Protobuf::Codec& codec = *entry->schema->codec;
    Protobuf::ColumnSet set;
    if (entry->filtering && entry->kept.messageIndex >= 0) {
        set = std::move(entry->kept);
        entry->kept = Protobuf::ColumnSet();
    } else {
        set = entry->decoder->take();
    }
    ArrayOfVector retval;
    ArrayOf presence;
    retval.push_back(rowsToValue(codec, set, settings, presence));
    if (nLhs > 1) {
        retval.push_back(presence);
    }
    if (nLhs > 2) {
        retval.push_back(
            decodeReport(entry->schema->schema.messages[(size_t)set.messageIndex].fullName,
                set.rows, 0, (int64_t)entry->decoder->pendingBytes()));
    }
    return retval;
}
//=============================================================================
