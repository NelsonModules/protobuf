%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function options = nameValueOptions(options, varargin)
  % Update the fields of options from name-value pairs (case-insensitive names).
  if mod(numel(varargin), 2) ~= 0
    error('protobuf:invalidOption:optionsNameValuePairs', 'Options must be name-value pairs.');
  end
  names = fieldnames(options);
  for k = 1:2:numel(varargin)
    match = strcmpi(names, char(string(varargin{k})));
    if ~any(match)
      error('protobuf:invalidOption:unknownOption', 'Unknown option: %s', char(string(varargin{k})));
    end
    options.(names{match}) = varargin{k + 1};
  end
end
%=============================================================================
