%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% NDJSON: one JSON object per line, from records or from a length-delimited stream.
%=============================================================================
sch = protobuf.load(['syntax = "proto3"; message M { int32 id = 1; string name = 2; ', ...
  'repeated double v = 3; }'], 'Format', 'text');
s = struct('id', {1, 2, 3}, 'name', {'a', '', 'c'}, 'v', {[1 2], [], 0.5});
txt = protobuf.tojson(sch, 'M', s, 'NDJSON', true);
asserts.isequal(txt, sprintf('{"id":1,"name":"a","v":[1.0,2.0]}\n{"id":2}\n{"id":3,"name":"c","v":[0.5]}\n'));
% Indent is ignored: one line per record.
asserts.isequal(protobuf.tojson(sch, 'M', s, 'NDJSON', true, 'Indent', true), txt);
% Bytes of a length-delimited stream.
bytes = protobuf.encode(sch, 'M', s, 'Delimited', true);
asserts.isequal(protobuf.tojson(sch, 'M', bytes, 'Delimited', true, 'NDJSON', true), txt);
asserts.isequal(protobuf.tojson(sch, 'M', bytes, 'Delimited', true), ...
  '[{"id":1,"name":"a","v":[1.0,2.0]},{"id":2},{"id":3,"name":"c","v":[0.5]}]');
% No record: empty text.
asserts.isequal(protobuf.tojson(sch, 'M', uint8([]), 'Delimited', true, 'NDJSON', true), '');
%=============================================================================
% Read back, blank lines and CRLF line ends included.
r = protobuf.fromjson(sch, 'M', txt, 'NDJSON', true);
asserts.isequal([r.id], int32([1 2 3]));
asserts.isequal(protobuf.encode(sch, 'M', r, 'Delimited', true), bytes);
crlf = strrep([newline, txt, newline], newline, sprintf('\r\n'));
[C, P, report] = protobuf.fromjson(sch, 'M', crlf, 'NDJSON', true, 'Layout', 'columns');
asserts.isequal(C.id, int32([1; 2; 3]));
asserts.isequal(C.name, {'a'; ''; 'c'});
asserts.isequal(report.Records, 3);
asserts.isequal(numel(protobuf.fromjson(sch, 'M', '', 'NDJSON', true)), 0);
%=============================================================================
% Errors name the line; nothing is returned on error.
asserts.checkerror('protobuf.fromjson(sch, ''M'', sprintf(''{"id":1}\n[{"id":2}]''), ''NDJSON'', true)', ...
  'line 2: a line must hold one JSON object.', 'protobuf:jsonError:lineNotObject');
try
  protobuf.fromjson(sch, 'M', sprintf('{"id":1}\n\n{"id":"x"}'), 'NDJSON', true);
  asserts.istrue(false);
catch err
  asserts.istrue(startsWith(err.message, 'line 3: '));
end
% Without the option, several objects on several lines are not one JSON document.
try
  protobuf.fromjson(sch, 'M', txt);
  asserts.istrue(false);
catch err
  asserts.isequal(err.identifier, 'protobuf:jsonError:unexpectedTextAfterJSONValue');
end
%=============================================================================
