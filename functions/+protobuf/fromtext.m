%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [value, presence, report] = fromtext(schema, type, txt, varargin)
  % Message of a protobuf text format document ("name: value", nested "name { }", comments with
  % #), returned like protobuf.decode with the same layout options.
  narginchk(3, Inf);
  protobuf.internal.checkSchema(schema);
  if ~(ischar(txt) || (isstring(txt) && isscalar(txt)))
    error('protobuf:invalidValue:textCharacterVectorStringScalar', ...
      'The text must be a character vector or a string scalar.');
  end
  options = protobuf.internal.decodeOptions(varargin{:});
  [value, presence, report] = __protobuf_fromtext__(schema.Id, char(type), char(txt), ...
    protobuf.internal.nativeOptions(options));
  value = protobuf.internal.finishLayout(value, options);
end
%=============================================================================
