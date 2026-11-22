%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [txt, fields] = inspect(bytes, varargin)
  % Protobuf data read without schema: field numbers, wire types and values, as
  % protoc --decode_raw prints them. With no output, the text is displayed.
  narginchk(1, Inf);
  options = protobuf.internal.nameValueOptions(struct('Delimited', false), varargin{:});
  [text, fields] = __protobuf_inspect__(bytes, logical(options.Delimited));
  if nargout == 0
    fprintf('%s', text);
  else
    txt = text;
  end
end
%=============================================================================
