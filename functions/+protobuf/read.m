%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function [value, presence, report] = read(schema, type, source, varargin)
  % Decode a protobuf file, the rest of an open file identifier, or an entry of an open
  % ZIP archive ('Entry', name); same options as protobuf.decode. A file is mapped in memory
  % rather than read; with 'Records', its record index (protobuf.index) is used when it is up
  % to date ('Index', file names another index file). 'Progress', @(n, total) is called after
  % each block of 8 MiB with the bytes read and the file size.
  narginchk(3, Inf);
  protobuf.internal.checkSchema(schema);
  [entry, options] = protobuf.internal.takeOption(varargin, 'Entry', '');
  [indexFile, options] = protobuf.internal.takeOption(options, 'Index', '');
  options = protobuf.internal.decodeOptions(options{:});
  if (~isempty(options.Where) || ~isempty(options.Progress)) && ~isa(source, 'nelson.io.ZipArchive')
    % Block by block: only the records kept by Where stay in memory.
    fid = source;
    if ~(isnumeric(source) && isscalar(source))
      [fid, message] = fopen(protobuf.internal.resolveFile(source), 'r');
      if fid < 0
        error('protobuf:fileNotFound:cannotOpen', 'Cannot open %s: %s', char(source), message);
      end
      cleanup = onCleanup(@() fclose(fid));
    end
    % Bytes left from the current position.
    position = ftell(fid);
    fseek(fid, 0, 'eof');
    byteCount = ftell(fid) - position;
    fseek(fid, position, 'bof');
    [value, presence, report] = protobuf.internal.filterRecords(schema, type, options, ...
      @() fread(fid, 8 * 1024 * 1024, '*uint8'), [], byteCount);
    return
  end
  if isa(source, 'nelson.io.ZipArchive')
    if isempty(entry)
      error('protobuf:invalidOption:readingZIPArchiveNeedsEntry', ...
        'Reading from a ZIP archive needs ''Entry'', the entry name.');
    end
    bytes = source.read(char(entry));
  elseif isnumeric(source) && isscalar(source)
    bytes = fread(source, Inf, '*uint8');
  else
    path = protobuf.internal.resolveFile(source);
    indexFile = char(string(indexFile));
    if isempty(indexFile) && isfile([path, '.pbidx'])
      indexFile = [path, '.pbidx'];
    end
    [value, presence, report] = __protobuf_decode_file__(schema.Id, char(type), path, ...
      protobuf.internal.nativeOptions(options), indexFile);
    value = protobuf.internal.finishLayout(value, options);
    return
  end
  [value, presence, report] = protobuf.internal.decodeBytes(schema, type, bytes, options);
end
%=============================================================================
