%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function txt = infer(value, varargin)
  % A .proto file (proto3 text) describing a struct, a struct array or a table, to start a
  % schema from data: 'Name' is the message name ('Record' by default), 'Package' its package.
  % Numbers give scalar fields (repeated when a value has several elements), text gives
  % string, uint8 gives bytes, datetime and duration give Timestamp and Duration, structs give
  % nested messages; a field empty in some records is optional.
  narginchk(1, Inf);
  defaults = struct('Name', 'Record', 'Package', '');
  options = protobuf.internal.nameValueOptions(defaults, varargin{:});
  name = char(string(options.Name));
  package = char(string(options.Package));
  if ~isvarname(name)
    error('protobuf:invalidOption:nameValidMessageName', 'Name must be a valid message name.');
  end
  if istable(value) || istimetable(value)
    samples = tableSamples(value);
  elseif isstruct(value)
    samples = structSamples({value});
  else
    error('protobuf:invalidValue:protobufInferExpectsStructStruct', ...
      'protobuf.infer expects a struct, a struct array or a table.');
  end
  imports = {};
  [lines, imports] = messageLines(name, samples, '', imports);
  header = {'syntax = "proto3";', ''};
  if ~isempty(package)
    header = [header, {['package ', package, ';'], ''}];
  end
  imports = unique(imports);
  for k = 1:numel(imports)
    header{end + 1} = ['import "', imports{k}, '";']; %#ok<AGROW>
  end
  if ~isempty(imports)
    header{end + 1} = '';
  end
  txt = sprintf('%s\n', header{:}, lines{:});
end
%=============================================================================
function samples = structSamples(structs)
  % Values of each field over all the elements of the structs: names and cells of values.
  names = {};
  values = {};
  for s = 1:numel(structs)
    current = structs{s};
    fields = fieldnames(current);
    for f = 1:numel(fields)
      index = find(strcmp(names, fields{f}), 1);
      if isempty(index)
        names{end + 1} = fields{f}; %#ok<AGROW>
        values{end + 1} = {}; %#ok<AGROW>
        index = numel(names);
      end
      for e = 1:numel(current)
        values{index}{end + 1} = current(e).(fields{f});
      end
    end
  end
  samples = struct('names', {names}, 'values', {values});
end
%=============================================================================
function samples = tableSamples(T)
  % Table variables: one value per row (a row of a matrix variable is one repeated value).
  names = T.Properties.VariableNames;
  values = cell(1, numel(names));
  for f = 1:numel(names)
    column = T.(names{f});
    rows = size(column, 1);
    values{f} = cell(1, rows);
    for r = 1:rows
      if iscell(column)
        values{f}{r} = column{r, :};
      else
        values{f}{r} = column(r, :);
      end
    end
  end
  samples = struct('names', {names}, 'values', {values});
end
%=============================================================================
function [lines, imports] = messageLines(name, samples, pad, imports)
  lines = {[pad, 'message ', name, ' {']};
  nested = {};
  for f = 1:numel(samples.names)
    field = samples.names{f};
    [label, typeName, sub, imports] = fieldType(field, samples.values{f}, [pad, '  '], imports);
    nested = [nested, sub]; %#ok<AGROW>
    lines{end + 1} = sprintf('%s  %s%s %s = %d;', pad, label, typeName, field, f); %#ok<AGROW>
  end
  if ~isempty(nested)
    lines = [lines(1), nested, lines(2:end)];
  end
  lines{end + 1} = [pad, '}'];
end
%=============================================================================
function [label, typeName, nested, imports] = fieldType(field, values, pad, imports)
  % Type of a field from its values; empty values only mark records where it is absent.
  nested = {};
  present = values(~cellfun(@isempty, values));
  absent = numel(present) < numel(values);
  if isempty(present)
    label = 'optional ';
    typeName = 'double';
    return
  end
  first = present{1};
  several = any(cellfun(@(v) isSeveral(v), present));
  if isstruct(first) || (iscell(first) && ~isempty(first) && isstruct(first{1}))
    structs = cellfun(@asStructs, present, 'UniformOutput', false);
    typeName = messageName(field);
    [nested, imports] = messageLines(typeName, structSamples(structs), pad, imports);
    label = repeatedLabel(several, '');
    return
  end
  if isdatetime(first) || isduration(first)
    if isdatetime(first)
      typeName = 'google.protobuf.Timestamp';
      imports{end + 1} = 'google/protobuf/timestamp.proto';
    else
      typeName = 'google.protobuf.Duration';
      imports{end + 1} = 'google/protobuf/duration.proto';
    end
    label = repeatedLabel(several, '');
    return
  end
  if ischar(first) || isstring(first) || iscellstr(first)
    typeName = 'string';
  elseif isa(first, 'uint8')
    typeName = 'bytes';
    several = any(cellfun(@iscell, present));
  elseif islogical(first)
    typeName = 'bool';
  elseif isnumeric(first) && isreal(first)
    typeName = numericType(class(first));
  else
    error('protobuf:invalidValue:protobufTypeFieldClass', ...
      'No protobuf type for field "%s" (class %s).', field, class(first));
  end
  optional = '';
  if absent
    optional = 'optional ';
  end
  label = repeatedLabel(several, optional);
end
%=============================================================================
function tf = isSeveral(value)
  % A value standing for several elements: text is one element, bytes are one element.
  if ischar(value) || isa(value, 'uint8')
    tf = false;
  elseif iscell(value)
    tf = true;
  else
    tf = numel(value) > 1;
  end
end
%=============================================================================
function s = asStructs(value)
  if iscell(value)
    s = [value{:}];
  else
    s = value;
  end
end
%=============================================================================
function label = repeatedLabel(several, fallback)
  if several
    label = 'repeated ';
  else
    label = fallback;
  end
end
%=============================================================================
function typeName = numericType(className)
  switch className
    case 'double'
      typeName = 'double';
    case 'single'
      typeName = 'float';
    case {'int8', 'int16', 'int32'}
      typeName = 'int32';
    case 'int64'
      typeName = 'int64';
    case {'uint16', 'uint32'}
      typeName = 'uint32';
    otherwise
      typeName = 'uint64';
  end
end
%=============================================================================
function name = messageName(field)
  % TitleCase message name of a field: sample_rate gives SampleRate.
  parts = strsplit(field, '_');
  parts = parts(~cellfun(@isempty, parts));
  for k = 1:numel(parts)
    part = parts{k};
    parts{k} = [upper(part(1)), part(2:end)];
  end
  name = [parts{:}];
end
%=============================================================================
