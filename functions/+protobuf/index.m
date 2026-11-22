%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function info = index(filename, varargin)
  % Write the record index of a length-delimited file (one offset every 'Step' records,
  % 1024 by default) next to it, as <file>.pbidx, or to 'IndexFile'. protobuf.read uses it to
  % reach the first record of 'Records' without reading the records before it, as long as the
  % file keeps the size and modification time it had when the index was written.
  % 'Progress', @(n, total) is called after each block of 64 MiB scanned (bytes, file size).
  narginchk(1, Inf);
  defaults = struct('Step', 1024, 'IndexFile', '', 'Progress', []);
  options = protobuf.internal.nameValueOptions(defaults, varargin{:});
  path = protobuf.internal.resolveFile(filename);
  indexFile = char(string(options.IndexFile));
  if isempty(indexFile)
    indexFile = [path, '.pbidx'];
  end
  step = options.Step;
  if ~(isnumeric(step) && isscalar(step) && isreal(step) && step >= 1 && step == fix(step))
    error('protobuf:invalidOption:stepPositiveInteger', 'Step must be a positive integer.');
  end
  progress = options.Progress;
  if isempty(progress)
    info = __protobuf_index__(path, indexFile, double(step));
  else
    protobuf.internal.checkProgress(progress);
    total = dir(path).bytes;
    state = [];
    done = false;
    while ~done
      [info, state] = __protobuf_index__(path, indexFile, double(step), state, 64 * 1024 * 1024);
      done = state.Done;
      feval(progress, min(state.Position, total), total);
    end
  end
  info.File = path;
  info.IndexFile = indexFile;
end
%=============================================================================
