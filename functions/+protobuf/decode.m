%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [value, presence, report] = decode(schema, type, bytes, varargin)
  % Decode protobuf bytes: one message, or length-delimited records with 'Delimited'.
  narginchk(3, Inf);
  protobuf.internal.checkSchema(schema);
  options = protobuf.internal.decodeOptions(varargin{:});
  [value, presence, report] = protobuf.internal.decodeBytes(schema, type, bytes, options);
end
%=============================================================================
