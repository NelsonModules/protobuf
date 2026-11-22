%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
classdef (Sealed) Writer < handle & nelson.mixin.CustomDisplay
  % Writes length-delimited records as they come: push values, then close. The target (a file
  % name, an open file identifier, or an entry of a ZIP archive) stays open in between.
  % 'Progress', @(n, total) is called after each push with the bytes written so far (total is
  % NaN: the writer does not know how much will come).
  properties (SetAccess = private)
    Type = ''
    Target = ''
    Layout = 'struct'
    Records = 0
    Bytes = 0
  end
  properties (Dependent)
    IsOpen
  end
  properties (SetAccess = private, Hidden)
    Schema = []
    Fid = -1
    OwnsFid = false
    EncodeOptions = {}
    Progress = []
  end
  methods
    function obj = Writer(schema, type, target, varargin)
      narginchk(3, Inf);
      protobuf.internal.checkSchema(schema);
      [append, options] = protobuf.internal.takeOption(varargin, 'Append', false);
      [entry, options] = protobuf.internal.takeOption(options, 'Entry', '');
      defaults = struct('Layout', 'struct', 'RowTimes', '', 'TimeUnit', 'ms', 'Progress', []);
      checked = protobuf.internal.nameValueOptions(defaults, options{:});
      if ~isempty(checked.Progress)
        protobuf.internal.checkProgress(checked.Progress);
      end
      obj.Progress = checked.Progress;
      obj.Layout = lower(char(string(checked.Layout)));
      if ~any(strcmp(obj.Layout, {'struct', 'columns'}))
        error('protobuf:invalidOption:layoutStructColumns', 'Layout must be one of: ''struct'', ''columns''.');
      end
      obj.EncodeOptions = {'Delimited', true, 'Layout', obj.Layout, 'RowTimes', checked.RowTimes, ...
        'TimeUnit', checked.TimeUnit};
      __protobuf_fields__(schema.Id, char(type));
      obj.Schema = schema;
      obj.Type = char(type);
      if isa(target, 'nelson.io.ZipArchive')
        if isempty(entry)
          error('protobuf:invalidOption:writingZIPArchiveNeedsEntry', ...
            'Writing to a ZIP archive needs ''Entry'', the entry name.');
        end
        obj.Fid = target.fopen(char(entry), 'w');
        obj.OwnsFid = true;
        obj.Target = char(entry);
      elseif isnumeric(target) && isscalar(target)
        obj.Fid = target;
        obj.Target = fopen(target);
      else
        mode = 'w';
        if logical(append)
          mode = 'a';
        end
        [fid, message] = fopen(target, mode);
        if fid < 0
          error('protobuf:cannotWrite:openWriting', 'Cannot open %s for writing: %s', char(target), message);
        end
        obj.Fid = fid;
        obj.OwnsFid = true;
        obj.Target = char(target);
      end
    end

    function push(obj, value)
      % Encodes value (a struct, a struct of columns, a table or a timetable) and writes it.
      if ~obj.IsOpen
        error('protobuf:cannotWrite:writerClosed', 'The writer is closed.');
      end
      bytes = protobuf.encode(obj.Schema, obj.Type, value, obj.EncodeOptions{:});
      fwrite(obj.Fid, bytes, 'uint8');
      obj.Records = obj.Records + recordCount(value, obj.Layout);
      obj.Bytes = obj.Bytes + numel(bytes);
      if ~isempty(obj.Progress)
        feval(obj.Progress, obj.Bytes, NaN);
      end
    end

    function close(obj)
      % Closes the target when the writer opened it (a file identifier given stays open).
      if obj.OwnsFid && obj.Fid >= 0
        fclose(obj.Fid);
      end
      obj.Fid = -1;
    end

    function tf = get.IsOpen(obj)
      tf = obj.Fid >= 0;
    end

    function delete(obj)
      obj.close();
    end
  end
end
%=============================================================================
function n = recordCount(value, layout)
  % Records of one pushed value: elements of a struct, rows of a table or of columns.
  if istable(value) || istimetable(value)
    n = height(value);
  elseif strcmp(layout, 'struct')
    n = numel(value);
  else
    n = columnRows(value);
  end
end
%=============================================================================
function n = columnRows(columns)
  n = 0;
  names = fieldnames(columns);
  for k = 1:numel(names)
    column = columns.(names{k});
    if isstruct(column) && isscalar(column)
      n = columnRows(column);
    else
      n = size(column, 1);
    end
    if n > 0
      return
    end
  end
end
%=============================================================================
