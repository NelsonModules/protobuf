%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% proto3 JSON details: special types, escapes, number forms, indentation and every error.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
js = protobuf.load([DATA, '/json_types.proto']);
anyh = protobuf.load([DATA, '/any_holder.proto']);
%=============================================================================
% Writing.
% A well-known type at the top level is written in its JSON form.
asserts.isequal(protobuf.tojson(js, 'google.protobuf.Timestamp', struct('seconds', 1, 'nanos', 5000)), ...
  '"1970-01-01T00:00:01.000005Z"');
asserts.isequal(protobuf.tojson(js, 'google.protobuf.Duration', struct('seconds', -1, 'nanos', -500000000)), ...
  '"-1.500s"');
% Outside the JSON range of the reference runtime.
asserts.checkerror('protobuf.tojson(js, ''google.protobuf.Timestamp'', struct(''seconds'', 253402300800))', ...
  'JSON: Timestamp out of the JSON range (years 1 to 9999).', 'protobuf:jsonError:timestampOutOfRange');
asserts.checkerror('protobuf.tojson(js, ''google.protobuf.Duration'', struct(''seconds'', -1, ''nanos'', 5))', ...
  'JSON: Duration out of the JSON range (+/-10000 years).', 'protobuf:jsonError:durationOutOfRange');
% Control characters, quotes and backslashes in strings.
asserts.isequal(protobuf.tojson(sch, 'AllTypes', struct('f_string', char([8 12 1 34 92 10 13 9 65]))), ...
  '{"fString":"\b\f\u0001\"\\\n\r\tA"}');
% Exponent forms of doubles and floats.
asserts.isequal(protobuf.tojson(sch, 'AllTypes', struct('f_double', 1e300, 'f_float', single(1e-7))), ...
  '{"fDouble":1e+300,"fFloat":1e-07}');
asserts.isequal(protobuf.tojson(sch, 'AllTypes', struct('f_double', 123456789012345678)), ...
  '{"fDouble":1.2345678901234568e+17}');
% A map entry without its message value writes the default message.
asserts.isequal(protobuf.tojson(sch, 'AllTypes', uint8([130 4 3 10 1 107])), '{"mStringSample":{"k":{}}}');
% A Struct entry without its Value writes null.
asserts.isequal(protobuf.tojson(js, 'Document', uint8([10 5 10 3 10 1 97])), '{"attributes":{"a":null}}');
% Indentation of nested objects, lists and maps.
doc = protobuf.fromjson(js, 'Document', ['{"attributes":{"x":[1,"a",null,{"y":true}]},"items":[],', ...
  '"named":{"k":false},"mask":"a.b,sampleRate"}'], 'WellKnownTypes', false);
asserts.isequal(protobuf.tojson(js, 'Document', doc, 'Indent', true), sprintf(['{\n  "attributes": {\n', ...
  '    "x": [\n      1.0,\n      "a",\n      null,\n      {\n        "y": true\n      }\n    ]\n  },\n', ...
  '  "items": [],\n  "mask": "a.b,sampleRate",\n  "named": {\n    "k": false\n  }\n}']));
asserts.isequal(doc.mask.paths, {'a.b', 'sample_rate'});
%=============================================================================
% Reading: forms accepted for numbers, enums, times and strings.
a = protobuf.fromjson(sch, 'AllTypes', ['{"f_double":"1.5","fFloat":"-Infinity","fInt64":"-12",', ...
  '"fUint32":7.0,"fEnum":2,"fString":"\b\fé🙂","fBool":false,"fBytes":"AQI-_w"}']);
asserts.isequal(a.f_double, 1.5);
asserts.isequal(a.f_float, single(-Inf));
asserts.isequal(a.f_int64, int64(-12));
asserts.isequal(a.f_uint32, uint32(7));
asserts.isequal(a.f_enum, int32(2));
asserts.isequal(double(a.f_string(1:2)), [8 12]);
asserts.isequal(unicode2native(a.f_string(3:end), 'UTF-8'), uint8([195 169 240 159 153 130]));
asserts.isequal(a.f_bytes, uint8([1 2 62 255]));
t = protobuf.fromjson(js, 'Document', ['{"at":"2026-11-12t10:30:15.5+01:30","times":["0001-01-01T00:00:00Z",', ...
  '"9999-12-31T23:59:59.999999999z"],"span":"0.000000001s","spans":["-315576000000s"]}'], 'WellKnownTypes', false);
asserts.isequal([t.at.seconds, t.at.nanos], [int64(1794474015), int64(500000000)]);
asserts.isequal(t.times(1).seconds, int64(-62135596800));
asserts.isequal(t.times(2).nanos, int32(999999999));
asserts.isequal(t.span.nanos, int32(1));
asserts.isequal(t.spans.seconds, int64(-315576000000));
% Null leaves a field unset, except in a Value.
d = protobuf.fromjson(js, 'Document', '{"value":null,"d":null,"values":[null]}', 'WellKnownTypes', false);
asserts.isequal(d.value.kind, 'null_value');
asserts.isequal(d.d, struct('value', {}));
%=============================================================================
% Errors.
% One case per line: schema, type, JSON text, expected error text.
schemas = struct('sch', sch, 'js', js, 'anyh', anyh);
lines = strsplit(fileread([DATA, '/json_errors.tsv']), newline);
lines = lines(~cellfun(@isempty, lines) & ~startsWith(lines, '#'));
cases = cell(numel(lines), 4);
for k = 1:numel(lines)
  cases(k, :) = strsplit(lines{k}, sprintf('\t'), 'CollapseDelimiters', false);
  cases{k, 1} = schemas.(cases{k, 1});
end
for k = 1:size(cases, 1)
  try
    protobuf.fromjson(cases{k, 1}, cases{k, 2}, cases{k, 3});
    error('test:json', 'no error for %s', cases{k, 3});
  catch err
    if ~startsWith(err.identifier, 'protobuf:jsonError:') || ~contains(err.message, cases{k, 4})
      error('test:json', 'For %s\n  expected: %s\n  got: %s (%s)', cases{k, 3}, cases{k, 4}, err.message, ...
        err.identifier);
    end
  end
end
%=============================================================================
