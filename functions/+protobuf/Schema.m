%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
classdef (Sealed) Schema < handle & nelson.mixin.CustomDisplay
  % Loaded .proto schema, used by protobuf.decode, encode, read and write.
  properties (SetAccess = private)
    File = ''
    Package = ''
    Messages = {}
    Imports = {}
  end
  properties (SetAccess = private, Hidden)
    Id = 0
  end
  methods
    function obj = Schema(source, importPaths, format, name)
      % format: 'proto' (a .proto file), 'descriptor' (a FileDescriptorSet file) or 'text'
      % (source is .proto text, called name in messages).
      narginchk(1, 4);
      if nargin < 2
        importPaths = {};
      end
      if nargin < 3
        format = 'proto';
      end
      if nargin < 4
        name = 'input.proto';
      end
      [id, info] = __protobuf_load__(char(source), importPaths, char(format), char(name));
      obj.Id = id;
      obj.File = info.File;
      obj.Package = info.Package;
      obj.Messages = info.Messages;
      obj.Imports = info.Imports;
    end

    function delete(obj)
      if obj.Id ~= 0
        __protobuf_release__(obj.Id);
        obj.Id = 0;
      end
    end
  end
end
%=============================================================================
