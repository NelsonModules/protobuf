%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function checkProgress(progress)
  % A 'Progress' option must be a function handle called as f(n, total).
  if ~isa(progress, 'function_handle')
    error('protobuf:invalidOption:progressFunctionHandleCalledF', ...
      'Progress must be a function handle called as f(n, total) with byte counts.');
  end
end
%=============================================================================
