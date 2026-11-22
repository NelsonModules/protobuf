//=============================================================================
// Copyright (c) 2016-present Allan CORNET (Nelson)
//=============================================================================
// This file is part of the protobuf module for Nelson.
// SPDX-License-Identifier: LGPL-3.0-or-later
//=============================================================================
#include "protobufEnumsBuiltin.hpp"
#include "Error.hpp"
#include "ErrorID.hpp"
#include "ProtobufError.hpp"
#include "InputOutputArgumentsCheckers.hpp"
#include "ProtobufRegistry.hpp"
//=============================================================================
using namespace Nelson;
//=============================================================================
// names = __protobuf_enums__(id): full names of the enum types of a schema;
// [names, numbers, fullName] = __protobuf_enums__(id, enumName): the values of one enum type,
// named in full or by a unique trailing part.
ArrayOfVector
Nelson::ProtobufGateway::protobufEnumsBuiltin(int nLhs, const ArrayOfVector& argIn)
{
    nargincheck(argIn, 1, 2);
    nargoutcheck(nLhs, 0, 3);
    std::shared_ptr<const SchemaEntry> entry = requireSchema(argIn[0].asDoubleScalar());
    const Protobuf::Schema& schema = entry->schema;
    ArrayOfVector retval;
    if (argIn.size() == 1) {
        stringVector names;
        for (const Protobuf::EnumDef& e : schema.enums) {
            names.push_back(e.fullName);
        }
        retval.push_back(ArrayOf::stringArray(names, Dimensions(names.size(), 1)));
        return retval;
    }
    const std::string name = argIn[1].asUtf8String();
    int32_t found = schema.findEnum(name);
    for (size_t k = 0; found < 0 && k < schema.enums.size(); ++k) {
        const std::string& full = schema.enums[k].fullName;
        if (full.size() > name.size()
            && full.compare(full.size() - name.size(), name.size(), name) == 0
            && full[full.size() - name.size() - 1] == '.') {
            for (size_t j = k + 1; j < schema.enums.size(); ++j) {
                const std::string& other = schema.enums[j].fullName;
                if (other.size() > name.size()
                    && other.compare(other.size() - name.size(), name.size(), name) == 0
                    && other[other.size() - name.size() - 1] == '.') {
                    Nelson::Error("protobuf:invalidType:enumNameAmbiguous",
                        "enum name \"%s\" is ambiguous (\"%s\", \"%s\").", name, full, other);
                }
            }
            found = (int32_t)k;
        }
    }
    if (found < 0) {
        Nelson::Error("protobuf:invalidType:unknownEnumType", "unknown enum type \"%s\".", name);
    }
    const Protobuf::EnumDef& e = schema.enums[(size_t)found];
    stringVector names;
    double* numbers = static_cast<double*>(
        ArrayOf::allocateArrayOf(NLS_DOUBLE, (indexType)e.values.size(), stringVector(), false));
    for (size_t k = 0; k < e.values.size(); ++k) {
        names.push_back(e.values[k].name);
        numbers[k] = (double)e.values[k].number;
    }
    const Dimensions column((indexType)e.values.size(), 1);
    retval.push_back(ArrayOf::stringArray(names, column));
    if (nLhs > 1) {
        retval.push_back(ArrayOf(NLS_DOUBLE, column, numbers));
    }
    if (nLhs > 2) {
        retval.push_back(ArrayOf::characterArray(e.fullName));
    }
    return retval;
}
//=============================================================================
