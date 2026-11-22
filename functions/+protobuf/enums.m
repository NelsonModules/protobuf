%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function T = enums(schema, type)
  % Enum types of a schema (full names), or the values of one enum type: a table with Name
  % and Number. The type is a full name or a unique trailing part such as 'Color'.
  narginchk(1, 2);
  protobuf.internal.checkSchema(schema);
  if nargin == 1
    T = __protobuf_enums__(schema.Id);
    return
  end
  [names, numbers] = __protobuf_enums__(schema.Id, char(type));
  T = table(names, numbers, 'VariableNames', {'Name', 'Number'});
end
%=============================================================================
