%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function schema = load(filename, varargin)
  % Parse a .proto file and the files it imports; no code generation and no compiler.
  % 'ImportPaths' lists folders where imports are searched after the folder of the file;
  % google/protobuf well-known types are built in. A FileDescriptorSet (protoc
  % --descriptor_set_out, extensions .binpb .pb .desc .protoset, or 'Format', 'descriptor')
  % is also accepted. With 'Format', 'text', filename is the .proto text itself ('Name' gives
  % the file name used in messages and by imports, 'input.proto' by default).
  narginchk(1, Inf);
  if ~(ischar(filename) || (isstring(filename) && isscalar(filename)))
    error('protobuf:invalidFilename:fileNameTextScalar', 'The file name must be a text scalar.');
  end
  defaults = struct('ImportPaths', {{}}, 'Format', 'auto', 'Name', 'input.proto');
  options = protobuf.internal.nameValueOptions(defaults, varargin{:});
  format = sourceFormat(filename, options.Format);
  if ~strcmp(format, 'text') && ~isfile(filename)
    error('protobuf:fileNotFound:noSuchFile', 'File not found: %s', char(filename));
  end
  schema = protobuf.Schema(filename, importPaths(options.ImportPaths), format, char(string(options.Name)));
end
%=============================================================================
function format = sourceFormat(filename, format)
  format = lower(char(string(format)));
  if ~any(strcmp(format, {'auto', 'proto', 'descriptor', 'text'}))
    error('protobuf:invalidOption:formatAutoProtoDescriptorText', ...
      'Format must be one of: ''auto'', ''proto'', ''descriptor'', ''text''.');
  end
  if strcmp(format, 'auto')
    [~, ~, extension] = fileparts(char(filename));
    format = 'proto';
    if any(strcmpi(extension, {'.binpb', '.pb', '.desc', '.protoset'}))
      format = 'descriptor';
    end
  end
end
%=============================================================================
function paths = importPaths(paths)
  if isempty(paths)
    paths = {};
  elseif ischar(paths) || isstring(paths) || iscellstr(paths)
    paths = reshape(cellstr(paths), 1, []);
  else
    error('protobuf:invalidOption:importpathsTextFolderListFolders', ...
      'ImportPaths must be text: a folder or a list of folders.');
  end
end
%=============================================================================
