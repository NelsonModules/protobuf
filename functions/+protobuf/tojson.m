%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function txt = tojson(schema, type, value, varargin)
  % Canonical proto3 JSON of a message (struct or uint8 bytes); a struct array gives a JSON array.
  % 'FieldNames', 'proto' keeps the .proto field names; 'Indent', true adds line breaks;
  % 'NDJSON', true writes one JSON object per line; 'Delimited', true reads the bytes as
  % length-delimited records.
  narginchk(3, Inf);
  protobuf.internal.checkSchema(schema);
  options = protobuf.internal.nameValueOptions(struct('FieldNames', 'json', 'Indent', false, ...
    'NDJSON', false, 'Delimited', false), varargin{:});
  names = lower(char(string(options.FieldNames)));
  if ~any(strcmp(names, {'json', 'proto'}))
    error('protobuf:invalidOption:fieldnamesJsonProto', 'FieldNames must be one of: ''json'', ''proto''.');
  end
  format = 'json';
  if logical(options.NDJSON)
    format = 'ndjson';
  end
  txt = __protobuf_format__(schema.Id, char(type), value, format, strcmp(names, 'proto'), ...
    logical(options.Indent), logical(options.Delimited));
end
%=============================================================================
