%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function T = diff(varargin)
  % Differences between two messages, field by field: protobuf.diff(a, b) for two structs,
  % protobuf.diff(schema, type, a, b) for structs or encoded bytes of a message type (both are
  % first read through the schema, so absent fields and default values compare the same way).
  % T has one row per difference: Path ("a.b(2).c"), Change ("changed", "added" in b,
  % "removed" from a), A and B (the values, [] when absent).
  narginchk(2, 4);
  if nargin == 2
    a = varargin{1};
    b = varargin{2};
  elseif nargin == 4
    schema = varargin{1};
    type = varargin{2};
    protobuf.internal.checkSchema(schema);
    a = canonical(schema, type, varargin{3});
    b = canonical(schema, type, varargin{4});
  else
    error('protobuf:invalidOption:protobufDiffTakesTwoStructs', ...
      'protobuf.diff takes two structs, or a schema, a type and two messages.');
  end
  if ~isstruct(a) || ~isstruct(b)
    error('protobuf:invalidValue:protobufDiffComparesStructsBytes', ...
      'protobuf.diff compares structs (or bytes with a schema).');
  end
  rows = compareValues(a, b, '', struct('Path', {}, 'Change', {}, 'A', {}, 'B', {}));
  T = table(string({rows.Path}'), string({rows.Change}'), {rows.A}', {rows.B}', ...
    'VariableNames', {'Path', 'Change', 'A', 'B'});
end
%=============================================================================
function value = canonical(schema, type, value)
  % The struct read back from the encoding: same fields and default values for both sides.
  if ~isa(value, 'uint8')
    value = protobuf.encode(schema, type, value);
  end
  value = protobuf.decode(schema, type, value);
end
%=============================================================================
function rows = compareValues(a, b, path, rows)
  if isstruct(a) && isstruct(b)
    if numel(a) == 1 && numel(b) == 1
      rows = compareStructs(a, b, path, rows);
      return
    end
    for k = 1:max(numel(a), numel(b))
      itemPath = sprintf('%s(%d)', path, k);
      if k > numel(a)
        rows = addRow(rows, itemPath, 'added', [], b(k));
      elseif k > numel(b)
        rows = addRow(rows, itemPath, 'removed', a(k), []);
      else
        rows = compareStructs(a(k), b(k), itemPath, rows);
      end
    end
    return
  end
  if isSame(a, b)
    return
  end
  if isempty(a) && ~isempty(b)
    rows = addRow(rows, path, 'added', a, b);
  elseif isempty(b) && ~isempty(a)
    rows = addRow(rows, path, 'removed', a, b);
  else
    rows = addRow(rows, path, 'changed', a, b);
  end
end
%=============================================================================
function rows = compareStructs(a, b, path, rows)
  names = unique([fieldnames(a); fieldnames(b)], 'stable');
  for k = 1:numel(names)
    name = names{k};
    fieldPath = name;
    if ~isempty(path)
      fieldPath = [path, '.', name];
    end
    if ~isfield(a, name)
      rows = addRow(rows, fieldPath, 'added', [], b.(name));
    elseif ~isfield(b, name)
      rows = addRow(rows, fieldPath, 'removed', a.(name), []);
    else
      rows = compareValues(a.(name), b.(name), fieldPath, rows);
    end
  end
end
%=============================================================================
function tf = isSame(a, b)
  % Same value (NaN equal to NaN); empty values of any class are the same absence.
  if isempty(a) && isempty(b)
    tf = true;
  else
    tf = strcmp(class(a), class(b)) && isequaln(a, b);
  end
end
%=============================================================================
function rows = addRow(rows, path, change, a, b)
  rows(end + 1) = struct('Path', path, 'Change', change, 'A', {a}, 'B', {b});
end
%=============================================================================
