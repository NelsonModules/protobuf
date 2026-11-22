%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function path = resolveFile(filename)
  % Full path of an existing file (relative names start from the current folder).
  name = char(string(filename));
  if ~isfile(name)
    error('protobuf:fileNotFound:noSuchFile', 'File not found: %s', name);
  end
  info = dir(name);
  path = [info(1).folder, '/', info(1).name];
end
%=============================================================================
