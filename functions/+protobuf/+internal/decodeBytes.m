%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [value, presence, report] = decodeBytes(schema, type, bytes, options)
  % Decode bytes with checked options (see decodeOptions) in the requested layout.
  if ~isempty(options.Where) || ~isempty(options.Progress)
    [value, presence, report] = protobuf.internal.filterRecords(schema, type, options, ...
      @(block) bytes(block), numel(bytes));
    return
  end
  [value, presence, report] = __protobuf_decode__(schema.Id, char(type), bytes, ...
    protobuf.internal.nativeOptions(options));
  value = protobuf.internal.finishLayout(value, options);
end
%=============================================================================
