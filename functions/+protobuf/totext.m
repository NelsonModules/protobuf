%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function txt = totext(schema, type, value)
  % Protobuf text format of a message given as a struct or as encoded uint8 bytes.
  narginchk(3, 3);
  protobuf.internal.checkSchema(schema);
  txt = __protobuf_format__(schema.Id, char(type), value, 'text', false, false, false);
end
%=============================================================================
