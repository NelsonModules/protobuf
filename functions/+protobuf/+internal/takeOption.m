%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [value, rest] = takeOption(options, name, value)
  % Remove the name-value pairs of one option (case-insensitive name); the last value wins.
  keep = true(size(options));
  for k = 1:2:numel(options) - 1
    key = options{k};
    if (ischar(key) || (isstring(key) && isscalar(key))) && strcmpi(key, name)
      value = options{k + 1};
      keep(k:k + 1) = false;
    end
  end
  rest = options(keep);
end
%=============================================================================
