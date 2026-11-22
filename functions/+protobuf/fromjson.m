%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [value, presence, report] = fromjson(schema, type, txt, varargin)
  % Records of a proto3 JSON text (an object, or an array of objects), returned like
  % protobuf.decode with the same layout options. 'NDJSON', true reads one object per line.
  narginchk(3, Inf);
  protobuf.internal.checkSchema(schema);
  if ~(ischar(txt) || (isstring(txt) && isscalar(txt)))
    error('protobuf:invalidValue:jsonTextCharacterVectorString', ...
      'The JSON text must be a character vector or a string scalar.');
  end
  [lines, varargin] = protobuf.internal.takeOption(varargin, 'NDJSON', false);
  options = protobuf.internal.decodeOptions(varargin{:});
  native = protobuf.internal.nativeOptions(options);
  native.NDJSON = logical(lines);
  [value, presence, report] = __protobuf_fromjson__(schema.Id, char(type), char(txt), native);
  value = protobuf.internal.finishLayout(value, options);
end
%=============================================================================
