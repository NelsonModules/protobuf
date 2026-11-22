//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufIndexBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufFileIdentity.hpp"
#include "ProtoMappedFile.hpp"
#include "ProtoRecordIndex.hpp"
#include "ProtoSchema.hpp"
#include "ProtoWire.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
//=============================================================================
using namespace Nelson;
//=============================================================================
// Writes the index file of path and returns its info struct.
static ArrayOf
writeIndex(const std::string& path, const std::string& indexPath, Protobuf::RecordIndex& index);
//=============================================================================
// info = __protobuf_index__(path, indexPath, step): writes the record index of a
// length-delimited file; info has Records, Bytes (of the complete records) and Step.
// [info, state] = __protobuf_index__(path, indexPath, step, state, blockBytes): one block of
// the scan; state ([] at first) has Position, Records, Offsets and Done, and info is [] until
// the last block, which writes the index.
ArrayOfVector
Nelson::ProtobufGateway::protobufIndexBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 3, 5);
    nargoutcheck(nLhs, 0, 2);
    const std::string path = argIn[0].asUtf8String();
    const std::string indexPath = argIn[1].asUtf8String();
    const double step = argIn[2].asDoubleScalar();
    if (!(step >= 1) || step != std::floor(step)) {
        Nelson::Error(
            "protobuf:invalidOption:stepPositiveInteger", "Step must be a positive integer.");
    }
    Protobuf::RecordIndex index;
    index.step = (uint64_t)step;
    size_t position = 0;
    bool done = true;
    const bool blocks = argIn.size() == 5;
    if (blocks && !argIn[3].isEmpty()) {
        const ArrayOf& state = argIn[3];
        position = (size_t)state.field("Position").asDoubleScalar();
        index.records = (uint64_t)state.field("Records").asDoubleScalar();
        ArrayOf offsets = state.field("Offsets");
        offsets.promoteType(NLS_UINT64);
        const uint64_t* values = static_cast<const uint64_t*>(offsets.dataPointer());
        index.offsets.clear();
        for (indexType k = 0; k < offsets.elementCount(); ++k) {
            index.offsets.push_back(values[k]);
        }
    }
    try {
        Protobuf::MappedFile file(path);
        const size_t limit = blocks
            ? (size_t)std::min((double)file.size(), (double)position + argIn[4].asDoubleScalar())
            : file.size();
        done = Protobuf::extendRecordIndex(index, file.data(), file.size(), position, limit);
    } catch (const Protobuf::ProtoError& e) {
        raiseProtoError(e, "protobuf:fileNotFound");
    }
    ArrayOfVector retval;
    if (blocks) {
        uint64_t* offsets = static_cast<uint64_t*>(ArrayOf::allocateArrayOf(
            NLS_UINT64, (indexType)index.offsets.size(), stringVector(), false));
        std::copy(index.offsets.begin(), index.offsets.end(), offsets);
        ArrayOfVector fields;
        fields.push_back(ArrayOf::doubleScalar((double)position));
        fields.push_back(ArrayOf::doubleScalar((double)index.records));
        fields.push_back(
            ArrayOf(NLS_UINT64, Dimensions((indexType)index.offsets.size(), 1), offsets));
        fields.push_back(ArrayOf::logicalScalar(done));
        const ArrayOf state
            = ArrayOf::structScalar({ "Position", "Records", "Offsets", "Done" }, fields);
        if (!done) {
            retval.push_back(ArrayOf::empty());
            retval.push_back(state);
            return retval;
        }
        retval.push_back(writeIndex(path, indexPath, index));
        retval.push_back(state);
        return retval;
    }
    retval.push_back(writeIndex(path, indexPath, index));
    return retval;
}
//=============================================================================
static ArrayOf
writeIndex(const std::string& path, const std::string& indexPath, Protobuf::RecordIndex& index)
{
    index.modified = ProtobufGateway::fileModificationTime(path);
    std::ofstream out(std::filesystem::u8path(indexPath), std::ios::binary | std::ios::trunc);
    const std::string bytes = Protobuf::encodeRecordIndex(index);
    out.write(bytes.data(), (std::streamsize)bytes.size());
    if (!out) {
        Nelson::Error(
            "protobuf:cannotWrite:writeIndexFile", "Cannot write the index file %s.", indexPath);
    }
    uint64_t used = 0;
    if (index.records > 0) {
        // End of the last complete record.
        Protobuf::MappedFile file(path);
        used = Protobuf::indexedRecordOffset(
            index, file.data(), file.size(), (size_t)index.records - 1);
        const uint8_t* p = file.data() + used;
        const uint64_t length = Protobuf::Wire::readVarint(p, file.data() + file.size());
        used = (uint64_t)(p - file.data()) + length;
    }
    ArrayOfVector values;
    values.push_back(ArrayOf::doubleScalar((double)index.records));
    values.push_back(ArrayOf::doubleScalar((double)used));
    values.push_back(ArrayOf::doubleScalar((double)index.step));
    return ArrayOf::structScalar({ "Records", "Bytes", "Step" }, values);
}
//=============================================================================
