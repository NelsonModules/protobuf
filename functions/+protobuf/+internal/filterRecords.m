%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [value, presence, report] = filterRecords(schema, type, options, nextBlock, total, byteCount)
  % Length-delimited records read block by block, keeping those accepted by options.Where.
  % nextBlock(range) returns the bytes of an index range when total is known, else
  % nextBlock() returns the next bytes ([] at the end). options.Progress(n, byteCount) is
  % called after each block with the bytes read so far (byteCount: total, NaN when unknown).
  if nargin < 6
    byteCount = total;
  end
  decoder = protobuf.Decoder(schema, type, options);
  progress = options.Progress;
  blockSize = 8 * 1024 * 1024;
  done = 0;
  if isempty(total)
    bytes = nextBlock();
    while ~isempty(bytes)
      decoder.push(bytes);
      done = done + numel(bytes);
      if ~isempty(progress)
        progress(done, byteCount);
      end
      bytes = nextBlock();
    end
  else
    for first = 1:blockSize:total
      last = min(first + blockSize - 1, total);
      decoder.push(nextBlock(first:last));
      if ~isempty(progress)
        progress(last, byteCount);
      end
    end
  end
  if done == 0 && isempty(total) && ~isempty(progress)
    progress(0, byteCount);
  end
  [value, presence, report] = decoder.read();
  if report.PendingBytes > 0 && strcmp(options.OnError, 'error')
    error('protobuf:decodeError:dataEndsInsideRecordBytes', ...
      'The data ends inside a record (%d bytes left).', report.PendingBytes);
  end
  report = rmfield(report, 'PendingBytes');
end
%=============================================================================
