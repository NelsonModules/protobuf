%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% protobuf.encode and protobuf.write: structs to bytes.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
s = struct('timestamp_ms', int64(1700000000000), 'status', 3, 'pressure', single(1.5), 'flow', [], ...
  'diagnostics', struct('calibrated', true, 'temperature', [], 'voltage', 12, 'drift', []));
r = protobuf.decode(sch, 'SensorSample', protobuf.encode(sch, 'SensorSample', s));
asserts.isequal(r.timestamp_ms, int64(1700000000000));
asserts.isequal(r.status, int32(3));
asserts.isequal(r.pressure, single(1.5));
asserts.isequal(r.flow, []);
asserts.isequal(r.diagnostics.calibrated, true);
asserts.isequal(r.diagnostics.voltage, single(12));
asserts.isequal(r.diagnostics.temperature, []);
% Missing struct fields keep their default value.
r = protobuf.decode(sch, 'SensorSample', protobuf.encode(sch, 'SensorSample', struct('pressure', 2)));
asserts.isequal(r.timestamp_ms, int64(0));
asserts.isequal(r.pressure, single(2));
asserts.istrue(isstruct(r.diagnostics) && isempty(r.diagnostics));
%=============================================================================
% Enum values by name, repeated values, maps and present empty text.
a = struct('f_enum', 'BLUE', 'r_enum', {{'RED', 'GREEN'}}, 'r_double', [1 2 3], 'o_string', '');
a.m_string_int32 = struct('key', {'a', 'b'}, 'value', {1, 2});
d = protobuf.decode(sch, 'AllTypes', protobuf.encode(sch, 'AllTypes', a));
asserts.isequal(d.f_enum, int32(-3));
asserts.isequal(d.r_enum, int32([1 2]));
asserts.isequal(d.r_double, [1 2 3]);
asserts.isequal(d.o_string, '');
asserts.isequal(d.o_int64, []);
asserts.isequal(d.m_string_int32(2).key, 'b');
asserts.isequal(d.m_string_int32(2).value, int32(2));
asserts.checkerror('protobuf.encode(sch, ''AllTypes'', struct(''f_enum'', ''PURPLE''))', ...
  '"PURPLE" is not a value of enum "nelson.test.sensors.AllTypes.Color".', 'protobuf:invalidValue:notAnEnumValue');
%=============================================================================
% Oneof: one member, or the member named in the oneof field.
msg = 'several members of oneof "choice" are set; keep one or name it in the "choice" field.';
asserts.checkerror('protobuf.encode(sch, ''AllTypes'', struct(''c_int32'', 1, ''c_string'', ''x''))', msg, ...
  'protobuf:invalidValue:severalMembersOneofSetKeep');
d = protobuf.decode(sch, 'AllTypes', ...
  protobuf.encode(sch, 'AllTypes', struct('c_int32', 1, 'c_string', 'x', 'choice', 'c_string')));
asserts.isequal(d.choice, 'c_string');
asserts.isequal(d.c_string, 'x');
asserts.isequal(d.c_int32, []);
d = protobuf.decode(sch, 'AllTypes', protobuf.encode(sch, 'AllTypes', struct('c_string', '', 'choice', 'c_string')));
asserts.isequal(d.choice, 'c_string');
asserts.isequal(d.c_string, '');
%=============================================================================
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', struct(''nope'', 1))', ...
  'unknown field "nope" for message "nelson.test.sensors.SensorSample".', 'protobuf:invalidValue:unknownFieldMessage');
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', struct(''timestamp_ms'', {1, 2}))', ...
  'A single message needs a scalar struct; use ''Delimited'' for several records.', ...
    'protobuf:notScalar:singleMessageNeedsScalarStruct');
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', struct(''status'', [1 2]))', ...
  'field "status" expects a scalar value.', 'protobuf:invalidValue:fieldExpectsScalarValue');
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', struct(''status'', ''x''))', ...
  'field "status" expects real numeric values.', 'protobuf:invalidValue:fieldExpectsRealNumericValues');
%=============================================================================
% Records written to a file and read back as columns.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
F = [WORK, '/records.bin'];
records = struct('timestamp_ms', {1, 2, 3}, 'flow', {single(0.5), [], single(2)});
protobuf.write(sch, 'SensorSample', F, records, 'Delimited', true);
[C, P] = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(C.timestamp_ms, int64([1; 2; 3]));
asserts.isequal(P.flow, [true; false; true]);
asserts.isequal(C.flow([1 3]), single([0.5; 2]));
asserts.istrue(isnan(C.flow(2)));
rmdir(WORK, 's');
%=============================================================================
% Numbers given as double for integer fields: rounded, saturated, NaN as absent.
C = struct('timestamp_ms', [1.4; 2.5; -2.5; 1e30; NaN], 'status', single([1.5; NaN; 3; -1e20; 5]));
R = protobuf.decode(sch, 'SensorSample', protobuf.encode(sch, 'SensorSample', C, 'Layout', 'columns', ...
  'Delimited', true), 'Delimited', true, 'Layout', 'columns', 'MissingAsNaN', true);
asserts.isequal(R.timestamp_ms, int64([1; 3; -3; intmax('int64'); 0]));
asserts.isequal(R.status, [2; NaN; 3; double(intmin('int32')); 5]);
% Several threads give the bytes of one.
C = struct('timestamp_ms', int64((1:1000)'), 'status', int32(mod((1:1000)', 7)));
one = protobuf.encode(sch, 'SensorSample', C, 'Layout', 'columns', 'Delimited', true, 'Workers', 1);
asserts.isequal(protobuf.encode(sch, 'SensorSample', C, 'Layout', 'columns', 'Delimited', true, 'Workers', 4), one);
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', C, ''Workers'', -1)', ...
  'Workers must be a nonnegative integer (0 chooses automatically).', 'protobuf:invalidOption:workersNotInteger');
%=============================================================================
