//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "NelsonGateway.hpp"
#include "ProtobufRegistry.hpp"
#include "protobufDecodeBuiltin.hpp"
#include "protobufDescriptorBuiltin.hpp"
#include "protobufDecoderCreateBuiltin.hpp"
#include "protobufDecoderPushBuiltin.hpp"
#include "protobufDecoderReadBuiltin.hpp"
#include "protobufDecoderReleaseBuiltin.hpp"
#include "protobufEncodeBuiltin.hpp"
#include "protobufFieldsBuiltin.hpp"
#include "protobufDecoderHoldBuiltin.hpp"
#include "protobufDecoderKeepBuiltin.hpp"
#include "protobufDecodeFileBuiltin.hpp"
#include "protobufIndexBuiltin.hpp"
#include "protobufDumpBuiltin.hpp"
#include "protobufEnumsBuiltin.hpp"
#include "protobufFormatBuiltin.hpp"
#include "protobufFromJsonBuiltin.hpp"
#include "protobufFromTextBuiltin.hpp"
#include "protobufInspectBuiltin.hpp"
#include "protobufLoadBuiltin.hpp"
#include "protobufReleaseBuiltin.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
const std::wstring gatewayName = L"protobuf";
//=============================================================================
static const nlsGateway gateway[] = {
    { "__protobuf_load__", (ptrBuiltin)Nelson::ProtobufGateway::protobufLoadBuiltin, 2, 3 },
    { "__protobuf_release__", (ptrBuiltin)Nelson::ProtobufGateway::protobufReleaseBuiltin, 0, 1 },
    { "__protobuf_decode__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDecodeBuiltin, 3, 4 },
    { "__protobuf_encode__", (ptrBuiltin)Nelson::ProtobufGateway::protobufEncodeBuiltin, 1, 7 },
    { "__protobuf_fields__", (ptrBuiltin)Nelson::ProtobufGateway::protobufFieldsBuiltin, 1, 2 },
    { "__protobuf_descriptor__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDescriptorBuiltin, 1,
        1 },
    { "__protobuf_format__", (ptrBuiltin)Nelson::ProtobufGateway::protobufFormatBuiltin, 1, 7 },
    { "__protobuf_fromjson__", (ptrBuiltin)Nelson::ProtobufGateway::protobufFromJsonBuiltin, 3, 4 },
    { "__protobuf_fromtext__", (ptrBuiltin)Nelson::ProtobufGateway::protobufFromTextBuiltin, 3, 4 },
    { "__protobuf_inspect__", (ptrBuiltin)Nelson::ProtobufGateway::protobufInspectBuiltin, 2, 2 },
    { "__protobuf_decoder_create__",
        (ptrBuiltin)Nelson::ProtobufGateway::protobufDecoderCreateBuiltin, 1, 3 },
    { "__protobuf_decoder_push__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDecoderPushBuiltin,
        2, 2 },
    { "__protobuf_decoder_read__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDecoderReadBuiltin,
        3, 2 },
    { "__protobuf_decoder_release__",
        (ptrBuiltin)Nelson::ProtobufGateway::protobufDecoderReleaseBuiltin, 0, 1 },
    { "__protobuf_decoder_hold__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDecoderHoldBuiltin,
        2, 2 },
    { "__protobuf_decoder_keep__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDecoderKeepBuiltin,
        0, 2 },
    { "__protobuf_decode_file__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDecodeFileBuiltin, 3,
        5 },
    { "__protobuf_index__", (ptrBuiltin)Nelson::ProtobufGateway::protobufIndexBuiltin, 2, 5 },
    { "__protobuf_dump__", (ptrBuiltin)Nelson::ProtobufGateway::protobufDumpBuiltin, 1, 3 },
    { "__protobuf_enums__", (ptrBuiltin)Nelson::ProtobufGateway::protobufEnumsBuiltin, 3, 2 },
};
//=============================================================================
static bool
finishProtobufModule(Nelson::Evaluator* eval)
{
    Nelson::ProtobufGateway::releaseAllSchemas();
    return true;
}
//=============================================================================
NLS_GATEWAY_FUNC(gateway)
//=============================================================================
NLS_GATEWAY_REMOVE_EXTENDED(gateway, (void*)finishProtobufModule)
//=============================================================================
