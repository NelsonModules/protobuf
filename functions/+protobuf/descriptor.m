%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function bytes = descriptor(schema)
  % Serialized FileDescriptorSet of a schema (the loaded file and its imports), as protoc
  % --descriptor_set_out --include_imports writes it.
  narginchk(1, 1);
  protobuf.internal.checkSchema(schema);
  bytes = __protobuf_descriptor__(schema.Id);
end
%=============================================================================
