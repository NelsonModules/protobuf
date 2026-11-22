%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function checkSchema(schema)
  % Raise an error unless schema is a loaded protobuf.Schema.
  if ~isa(schema, 'protobuf.Schema') || ~isscalar(schema) || ~isvalid(schema)
    error('protobuf:invalidSchema:expectedSchemaReturnedProtobufLoad', 'Expected a schema returned by protobuf.load.');
  end
end
%=============================================================================
