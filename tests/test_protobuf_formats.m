%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Text format and proto3 JSON.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
v = struct('timestamp_ms', 5, 'pressure', single(1.5), 'diagnostics', struct('calibrated', true));
expected = sprintf('timestamp_ms: 5\npressure: 1.5\ndiagnostics {\n  calibrated: true\n}\n');
asserts.isequal(protobuf.totext(sch, 'SensorSample', v), expected);
asserts.isequal(protobuf.totext(sch, 'SensorSample', protobuf.encode(sch, 'SensorSample', v)), expected);
j = protobuf.tojson(sch, 'SensorSample', v);
asserts.isequal(j, '{"timestampMs":"5","pressure":1.5,"diagnostics":{"calibrated":true}}');
asserts.isequal(protobuf.tojson(sch, 'SensorSample', v, 'FieldNames', 'proto'), ...
  '{"timestamp_ms":"5","pressure":1.5,"diagnostics":{"calibrated":true}}');
back = protobuf.fromjson(sch, 'SensorSample', j);
asserts.isequal(protobuf.encode(sch, 'SensorSample', back), protobuf.encode(sch, 'SensorSample', v));
asserts.isequal(jsondecode(protobuf.tojson(sch, 'SensorSample', [v, v])), jsondecode(['[', j, ',', j, ']']));
C = protobuf.fromjson(sch, 'SensorSample', ['[', j, ',', j, ']'], 'Layout', 'columns');
asserts.isequal(C.timestamp_ms, int64([5; 5]));
% Every golden record: JSON read back gives the same bytes.
fid = fopen([DATA, '/golden/sensor_stream.nelson.test.sensors.AllTypes.binpb'], 'r');
raw = fread(fid, Inf, '*uint8')';
fclose(fid);
records = protobuf.decode(sch, 'AllTypes', raw, 'Delimited', true);
again = protobuf.fromjson(sch, 'AllTypes', protobuf.tojson(sch, 'AllTypes', records));
asserts.isequal(protobuf.encode(sch, 'AllTypes', again, 'Delimited', true), raw);
%=============================================================================
w = protobuf.load([DATA, '/well_known.proto']);
t = datetime(2026, 1, 2, 3, 4, 5.5, 'TimeZone', 'UTC');
asserts.isequal(protobuf.tojson(w, 'Reading', struct('at', t, 'elapsed', seconds(1.25), 'count', 3)), ...
  '{"at":"2026-01-02T03:04:05.500Z","elapsed":"1.250s","count":3}');
r = protobuf.fromjson(w, 'Reading', '{"at":"2026-01-02T04:04:05.5+01:00","label":"x","marks":[]}');
asserts.isequal(posixtime(r.at), posixtime(t));
asserts.isequal(r.label, 'x');
%=============================================================================
asserts.checkerror('protobuf.fromjson(sch, ''SensorSample'', ''{"nope":1}'')', ...
  'JSON: unknown field "nope" for message "nelson.test.sensors.SensorSample".', ...
    'protobuf:jsonError:unknownFieldMessage');
asserts.checkerror('protobuf.fromjson(sch, ''SensorSample'', ''{"status":'')', ...
  'JSON: unexpected end of text at character 11.', 'protobuf:jsonError:unexpectedEndText');
asserts.checkerror('protobuf.tojson(sch, ''SensorSample'', v, ''FieldNames'', ''x'')', ...
  'FieldNames must be one of: ''json'', ''proto''.', 'protobuf:invalidOption:fieldnamesJsonProto');
%=============================================================================
