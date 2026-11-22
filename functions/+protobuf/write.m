%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function write(schema, type, target, value, varargin)
  % Encode records and write them to a file name, an open file identifier, or a new entry
  % of a ZIP archive open for writing ('Entry', name); same options as protobuf.encode.
  % 'Append', true adds the records at the end of an existing file. 'Progress', @(n, total) is
  % called after each block of 8 MiB written (bytes written, bytes to write).
  narginchk(4, Inf);
  [append, options] = protobuf.internal.takeOption(varargin, 'Append', false);
  [entry, options] = protobuf.internal.takeOption(options, 'Entry', '');
  [progress, options] = protobuf.internal.takeOption(options, 'Progress', []);
  if ~isempty(progress)
    protobuf.internal.checkProgress(progress);
  end
  bytes = protobuf.encode(schema, type, value, options{:});
  if isa(target, 'nelson.io.ZipArchive')
    if isempty(entry)
      error('protobuf:invalidOption:writingZIPArchiveNeedsEntry', ...
        'Writing to a ZIP archive needs ''Entry'', the entry name.');
    end
    target.write(char(entry), bytes);
    reportProgress(progress, numel(bytes), numel(bytes));
    return
  end
  if isnumeric(target) && isscalar(target)
    writeBlocks(target, bytes, progress);
    return
  end
  mode = 'w';
  if logical(append)
    mode = 'a';
  end
  [fid, message] = fopen(target, mode);
  if fid < 0
    error('protobuf:cannotWrite:openWriting', 'Cannot open %s for writing: %s', char(target), message);
  end
  cleanup = onCleanup(@() fclose(fid));
  writeBlocks(fid, bytes, progress);
end
%=============================================================================
function writeBlocks(fid, bytes, progress)
  % Bytes written in blocks of 8 MiB when progress is asked for.
  if isempty(progress)
    fwrite(fid, bytes, 'uint8');
    return
  end
  total = numel(bytes);
  blockSize = 8 * 1024 * 1024;
  for first = 1:blockSize:max(total, 1)
    last = min(first + blockSize - 1, total);
    fwrite(fid, bytes(first:last), 'uint8');
    feval(progress, last, total);
  end
end
%=============================================================================
function reportProgress(progress, n, total)
  if ~isempty(progress)
    feval(progress, n, total);
  end
end
%=============================================================================
