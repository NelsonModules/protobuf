//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufDecoderKeepBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufConversion.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// __protobuf_decoder_keep__(id, mask): the held rows whose mask value is true join the rows
// returned by the next read.
ArrayOfVector
Nelson::ProtobufGateway::protobufDecoderKeepBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 2, 2);
    nargoutcheck(nLhs, 0, 0);
    std::shared_ptr<DecoderEntry> entry = requireDecoder(argIn[0].asDoubleScalar());
    ArrayOf mask = argIn[1];
    const size_t rows = entry->held.rows;
    if (!(mask.isLogical() || mask.isNumeric()) || (size_t)mask.elementCount() != rows
        || (rows > 0 && !mask.isVector())) {
        Nelson::Error("protobuf:invalidOption:whereReturnLogicalValuePer",
            "Where must return one logical value per record (%d records in this block).", rows);
    }
    mask.promoteType(NLS_LOGICAL);
    const logical* flags = static_cast<const logical*>(mask.dataPointer());
    std::vector<uint8_t> selected(rows, 0);
    for (size_t i = 0; i < rows; ++i) {
        selected[i] = flags[i] != 0 ? 1 : 0;
    }
    const Protobuf::Codec& codec = *entry->schema->codec;
    try {
        Protobuf::ColumnSet part = codec.selectRows(entry->held, selected);
        if (entry->kept.messageIndex < 0) {
            entry->kept = std::move(part);
        } else {
            std::vector<Protobuf::ColumnSet*> parts = { &part };
            codec.appendRows(entry->kept, parts);
        }
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:decodeError");
    }
    entry->held = Protobuf::ColumnSet();
    return { };
}
//=============================================================================
