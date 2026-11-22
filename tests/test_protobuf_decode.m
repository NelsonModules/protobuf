%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% protobuf.decode and protobuf.read: struct and column layouts.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
SAMPLES = [DATA, '/golden/sensor_stream.nelson.test.sensors.SensorSample.binpb'];
[S, P, R] = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true);
asserts.isequal(size(S), [1 25]);
asserts.isequal(R.Type, 'nelson.test.sensors.SensorSample');
asserts.isequal(R.Records, 25);
asserts.isequal(R.Skipped, 0);
asserts.isequal(S(1).timestamp_ms, int64(-1));
asserts.isequal(S(1).status, int32(1));
asserts.isequal(class(S(1).pressure), 'single');
% Absent optional values are [].
asserts.isequal(S(1).flow, []);
asserts.isequal(S(1).diagnostics.calibrated, []);
asserts.isequal(S(1).diagnostics.temperature, single(-Inf));
asserts.istrue(isnan(S(1).diagnostics.drift));
%=============================================================================
% Columns: one Nx1 array per field, NaN for missing floating-point values.
[C, P] = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(class(C.timestamp_ms), 'int64');
asserts.isequal(size(C.timestamp_ms), [25 1]);
asserts.isequal(C.timestamp_ms(1), int64(-1));
asserts.isequal(class(C.flow), 'single');
asserts.isequal(class(P.flow), 'logical');
asserts.istrue(isnan(C.flow(1)));
asserts.isfalse(P.flow(1));
asserts.isequal(class(C.diagnostics.drift), 'double');
asserts.isequal(class(C.status), 'int32');
asserts.isequal(C.status(~P.status), zeros(nnz(~P.status), 1, 'int32'));
asserts.isequal(C.status(P.status), [S(P.status).status]');
% MissingAsNaN turns integer columns that can be missing into double.
C2 = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Layout', 'columns', 'MissingAsNaN', true);
asserts.isequal(class(C2.status), 'double');
asserts.isequal(isnan(C2.status), ~P.status);
asserts.isequal(class(C2.timestamp_ms), 'int64');
%=============================================================================
% Oneof: the active member name is in the oneof field.
EVENTS = [DATA, '/golden/sensor_stream.nelson.test.sensors.Event.binpb'];
E = protobuf.read(sch, 'nelson.test.sensors.Event', EVENTS, 'Delimited', true);
asserts.isequal(E(1).kind, 'step_completed');
% An absent sub-message is an empty struct with the message fields.
asserts.istrue(isstruct(E(1).started) && isempty(E(1).started));
asserts.isequal(fieldnames(E(1).started), {'timestamp_ms'; 'mode'});
asserts.isequal(class(E(1).step_completed.measurements), 'struct');
EC = protobuf.read(sch, 'Event', EVENTS, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(EC.kind, {E.kind}');
%=============================================================================
% Every golden stream decodes and encodes back to the same bytes.
files = dir([DATA, '/golden/sensor_stream.*.binpb']);
asserts.isequal(numel(files), 10);
for k = 1:numel(files)
  name = files(k).name;
  type = name(numel('sensor_stream.') + 1:end - numel('.binpb'));
  fid = fopen([DATA, '/golden/', name], 'r');
  raw = fread(fid, Inf, '*uint8')';
  fclose(fid);
  values = protobuf.decode(sch, type, raw, 'Delimited', true);
  asserts.isequal(protobuf.encode(sch, type, values, 'Delimited', true), raw);
  columns = protobuf.decode(sch, type, raw, 'Delimited', true, 'Layout', 'columns');
  asserts.istrue(isstruct(columns));
end
%=============================================================================
% Single message, errors and skipped records.
one = protobuf.encode(sch, 'SensorSample', struct('timestamp_ms', 150));
asserts.isequal(one, uint8([8 150 1]));
asserts.isequal(protobuf.decode(sch, 'SensorSample', one).timestamp_ms, int64(150));
asserts.checkerror('protobuf.decode(sch, ''Nope'', one)', 'unknown message type "Nope".', ...
  'protobuf:parseError:unknownMessageType');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', [1 2])', 'The encoded data must be a uint8 vector.', ...
  'protobuf:invalidBytes:encodedDataUint8Vector');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', one, ''Layout'', ''cube'')', ...
  'Layout must be one of: ''struct'', ''columns'', ''table'', ''timetable''.', 'protobuf:invalidOption:mustBeOneOf');
fid = fopen(SAMPLES, 'r');
raw = fread(fid, Inf, '*uint8')';
fclose(fid);
cut = raw(1:end - 2);
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', cut, ''Delimited'', true)', ...
  'record 25: truncated length-delimited field in the input.', ...
    'protobuf:decodeError:truncatedLengthDelimitedFieldInput');
[V, ~, R] = protobuf.decode(sch, 'SensorSample', cut, 'Delimited', true, 'OnError', 'skip');
asserts.isequal(R.Records, 24);
asserts.isequal(R.Skipped, 1);
asserts.isequal(numel(V), 24);
%=============================================================================
