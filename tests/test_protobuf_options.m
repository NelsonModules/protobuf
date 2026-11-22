%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Field selection, record ranges, table and timetable layouts, schema introspection.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
SAMPLES = [DATA, '/golden/sensor_stream.nelson.test.sensors.SensorSample.binpb'];
EVENTS = [DATA, '/golden/sensor_stream.nelson.test.sensors.Event.binpb'];
all = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true);
C = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Layout', 'columns');
%=============================================================================
% Fields: only the selected top-level fields are decoded and returned.
S = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Fields', {'pressure', 'timestamp_ms'});
asserts.isequal(fieldnames(S), {'timestamp_ms'; 'pressure'});
asserts.isequal([S.timestamp_ms], [all.timestamp_ms]);
[K, P] = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Layout', 'columns', 'Fields', "diagnostics");
asserts.isequal(fieldnames(K), {'diagnostics'});
asserts.isequal(K.diagnostics.drift, C.diagnostics.drift);
asserts.isequal(fieldnames(P), {'diagnostics'});
E = protobuf.read(sch, 'Event', EVENTS, 'Delimited', true, 'Fields', ["started", "stopped"]);
asserts.isequal(fieldnames(E), {'started'; 'stopped'; 'kind'});
asserts.checkerror('protobuf.read(sch, ''SensorSample'', SAMPLES, ''Delimited'', true, ''Fields'', ''nope'')', ...
  'unknown field "nope" for message "nelson.test.sensors.SensorSample".', 'protobuf:invalidOption:unknownFieldMessage');
%=============================================================================
% Records: [first last], last can be Inf; errors keep the record numbers of the stream.
[R, ~, report] = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Records', [3 5]);
asserts.isequal([R.timestamp_ms], [all(3:5).timestamp_ms]);
asserts.isequal(report.Records, 3);
R = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Records', [24 Inf]);
asserts.isequal(numel(R), 2);
R = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Records', [26 Inf]);
asserts.isequal(numel(R), 0);
asserts.checkerror('protobuf.read(sch, ''SensorSample'', SAMPLES, ''Records'', [1 2])', ...
  'Records needs ''Delimited'', true.', 'protobuf:invalidOption:recordsNeedsDelimitedTrue');
asserts.checkerror('protobuf.read(sch, ''SensorSample'', SAMPLES, ''Delimited'', true, ''Records'', [0 2])', ...
  'Records must be [first last], positive record numbers (last can be Inf).', ...
    'protobuf:invalidOption:recordsFirstLastPositiveRecord');
%=============================================================================
% Table and timetable layouts: sub-message columns become parent_child variables.
T = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Layout', 'table');
asserts.isequal(size(T), [25 8]);
asserts.istrue(any(strcmp(T.Properties.VariableNames, 'diagnostics_drift')));
asserts.isequal(T.diagnostics_drift, C.diagnostics.drift);
TT = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Layout', 'timetable', ...
  'RowTimes', 'status', 'TimeUnit', 's', 'Fields', 'pressure');
asserts.isequal(TT.Properties.VariableNames, {'pressure'});
expected = datetime(double(C.status), 'ConvertFrom', 'posixtime', 'TimeZone', 'UTC');
asserts.isequal(TT.Properties.RowTimes, expected);
EC = protobuf.read(sch, 'Event', EVENTS, 'Delimited', true, 'Layout', 'table');
asserts.isequal(class(EC.kind), 'string');
asserts.checkerror('protobuf.read(sch, ''SensorSample'', SAMPLES, ''Delimited'', true, ''Layout'', ''timetable'')', ...
  'The timetable layout needs RowTimes, the name of a time field.', ...
    'protobuf:invalidOption:timetableLayoutNeedsRowTimesName');
%=============================================================================
% Decoder with Fields and a table layout.
fid = fopen(SAMPLES, 'r');
raw = fread(fid, Inf, '*uint8')';
fclose(fid);
d = protobuf.Decoder(sch, 'SensorSample', 'Layout', 'table', 'Fields', 'flow');
d.push(raw);
DT = d.read();
asserts.isequal(DT.Properties.VariableNames, {'flow'});
asserts.isequal(DT.flow, C.flow);
%=============================================================================
% A sub-message present with no field set is told from an absent one.
sub = protobuf.load('syntax = "proto3"; message P { int32 x = 1; } message M { int32 a = 1; P p = 2; }', ...
  'Format', 'text');
subRaw = [protobuf.encode(sub, 'M', struct('a', 1, 'p', struct('x', 5)), 'Delimited', true), uint8([2 18 0 2 8 3])];
[subC, subP] = protobuf.decode(sub, 'M', subRaw, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(subC.p.x, int32([5; 0; 0]));
asserts.isequal(subP.p.Present, [true; true; false]);
asserts.isequal(protobuf.encode(sub, 'M', subC, 'Layout', 'columns', 'Presence', subP, 'Delimited', true), subRaw);
subP.p.Present(:) = false;
asserts.isequal(protobuf.encode(sub, 'M', subC, 'Layout', 'columns', 'Presence', subP, 'Delimited', true), ...
  uint8([2 8 1 0 2 8 3]));
% A message with a field named Present gets Present_.
own = protobuf.load(['syntax = "proto3"; message Q { bool Present = 1; } message N { Q q = 1; }'], 'Format', 'text');
[~, ownP] = protobuf.decode(own, 'N', uint8([2 10 0]), 'Delimited', true, 'Layout', 'columns');
asserts.isequal(fieldnames(ownP.q), {'Present'; 'Present_'});
asserts.isequal(ownP.q.Present_, true);
%=============================================================================
% Field table of a message type.
F = protobuf.fields(sch, 'AllTypes');
asserts.isequal(F.Properties.VariableNames, ...
  {'Name', 'Number', 'Type', 'TypeName', 'Repeated', 'Map', 'Presence', 'Oneof', 'JsonName', 'Required', ...
  'Default', 'Options', 'Extension'});
asserts.isequal(F.Oneof(F.Name == "c_sample"), "choice");
asserts.isequal(F.TypeName(F.Name == "c_sample"), "nelson.test.sensors.SensorSample");
asserts.isequal(F.JsonName(F.Name == "f_double"), "fDouble");
asserts.istrue(F.Map(F.Name == "m_string_int32"));
%=============================================================================
% Encoding columns, tables and timetables: values round trip; with the presence output, the
% columns give back the same bytes (Present tells the records that have a sub-message).
files = dir([DATA, '/golden/sensor_stream.*.binpb']);
for k = 1:numel(files)
  name = files(k).name;
  type = name(numel('sensor_stream.') + 1:end - numel('.binpb'));
  fid = fopen([DATA, '/golden/', name], 'r');
  raw = fread(fid, Inf, '*uint8')';
  fclose(fid);
  [C1, P1] = protobuf.decode(sch, type, raw, 'Delimited', true, 'Layout', 'columns');
  bytes = protobuf.encode(sch, type, C1, 'Layout', 'columns', 'Presence', P1, 'Delimited', true);
  asserts.isequal(bytes, raw);
  [C2, P2] = protobuf.decode(sch, type, bytes, 'Delimited', true, 'Layout', 'columns');
  asserts.istrue(isequaln(C1, C2) && isequal(P1, P2));
  T1 = protobuf.decode(sch, type, raw, 'Delimited', true, 'Layout', 'table');
  T2 = protobuf.decode(sch, type, protobuf.encode(sch, type, T1, 'Delimited', true), 'Delimited', true, 'Layout', ...
    'table');
  asserts.istrue(isequaln(T1, T2));
end
TT = protobuf.read(sch, 'SensorSample', SAMPLES, 'Delimited', true, 'Layout', 'timetable', ...
  'RowTimes', 'status', 'TimeUnit', 's');
back = protobuf.decode(sch, 'SensorSample', protobuf.encode(sch, 'SensorSample', TT, 'Delimited', true, ...
  'RowTimes', 'status', 'TimeUnit', 's'), 'Delimited', true, 'Layout', 'columns');
asserts.isequal(back.status, C.status);
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', 1, ''Layout'', ''columns'')', ...
  'The columns layout needs a scalar struct of Nx1 columns.', 'protobuf:invalidValue:columnsLayoutNeedsColumns');
%=============================================================================
% KeepUnknown: an older schema keeps the fields it does not know and writes them back.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
filewrite([WORK, '/older.proto'], sprintf(['syntax = "proto3";\npackage nelson.test.sensors;\n', ...
  'message SensorSample {\n  message Diagnostics { optional bool calibrated = 1; }\n', ...
  '  int64 timestamp_ms = 1;\n  optional Diagnostics diagnostics = 5;\n}\n']));
older = protobuf.load([WORK, '/older.proto']);
fid = fopen(SAMPLES, 'r');
raw = fread(fid, Inf, '*uint8')';
fclose(fid);
S = protobuf.decode(older, 'SensorSample', raw, 'Delimited', true, 'KeepUnknown', true);
asserts.isequal(fieldnames(S), {'timestamp_ms'; 'diagnostics'; 'UnknownFields'});
asserts.isequal(class(S(1).UnknownFields), 'uint8');
again = protobuf.encode(older, 'SensorSample', S, 'Delimited', true);
asserts.isequal(numel(again), numel(raw));
asserts.isequal(protobuf.decode(sch, 'SensorSample', again, 'Delimited', true, 'Layout', 'columns'), C);
asserts.isequal(fieldnames(protobuf.decode(older, 'SensorSample', raw, 'Delimited', true)), ...
  {'timestamp_ms'; 'diagnostics'});
[K, KP] = protobuf.decode(older, 'SensorSample', raw, 'Delimited', true, 'KeepUnknown', true, 'Layout', 'columns', ...
  'Workers', 3);
asserts.isequal(class(K.UnknownFields), 'cell');
again = protobuf.encode(older, 'SensorSample', K, 'Layout', 'columns', 'Presence', KP, 'Delimited', true);
asserts.isequal(protobuf.decode(sch, 'SensorSample', again, 'Delimited', true, 'Layout', 'columns'), C);
rmdir(WORK, 's');
%=============================================================================
% Columns encoding checks.
bad = struct('timestamp_ms', int64([1; 2]), 'nope', [1; 2]);
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', bad, ''Layout'', ''columns'', ''Delimited'', true)', ...
  'unknown field "nope" for message "nelson.test.sensors.SensorSample".', 'protobuf:invalidValue:unknownFieldMessage');
bad = struct('timestamp_ms', int64([1; 2]), 'status', [1; 2; 3]);
asserts.checkerror('protobuf.encode(sch, ''SensorSample'', bad, ''Layout'', ''columns'', ''Delimited'', true)', ...
  'column "status" must have 2 rows.', 'protobuf:invalidValue:columnRows');
bad = struct('r_double', [1; 2]);
asserts.checkerror('protobuf.encode(sch, ''AllTypes'', bad, ''Layout'', ''columns'', ''Delimited'', true)', ...
  'column "r_double" expects an Nx1 cell (one value per record).', 'protobuf:invalidValue:columnExpectsNx1CellValue');
cols = struct('timestamp_ms', int64([7; 8]), 'flow', single([NaN; 2]));
asserts.isequal(protobuf.decode(sch, 'SensorSample', ...
  protobuf.encode(sch, 'SensorSample', cols, 'Layout', 'columns', 'Delimited', true), 'Delimited', true), ...
  struct('timestamp_ms', {int64(7), int64(8)}, 'status', [], 'pressure', [], 'flow', {[], single(2)}, ...
  'diagnostics', protobuf.decode(sch, 'SensorSample', uint8([])).diagnostics));
%=============================================================================
% Other field options, custom ones included, as written in the .proto text.
custom = protobuf.load(['syntax = "proto3"; ', ...
  'message M { double p = 1 [(units) = "mbar", (range).max = 80, deprecated = true]; int32 q = 2; }'], ...
  'Format', 'text');
F = protobuf.fields(custom, 'M');
asserts.isequal({F.Options{1}.Name}, {"(units)", "(range).max", "deprecated"});
asserts.isequal({F.Options{1}.Value}, {"mbar", "80", "true"});
asserts.isequal(size(F.Options{2}), [1 0]);
% Without google/protobuf/descriptor.proto, option values are not decoded.
asserts.isequal({F.Options{1}.Decoded}, {[], [], []});
%=============================================================================
% With the definitions of the options, Decoded holds the value with its type.
typed = protobuf.load(['syntax = "proto3"; package p.q; import "google/protobuf/descriptor.proto"; ', ...
  'enum Lvl { L0 = 0; HIGH = 2; } message Range { double min = 1; double max = 2; } ', ...
  'extend google.protobuf.FieldOptions { string units = 50000; Range range = 50001; Lvl lvl = 50002; ', ...
  'repeated int32 tags = 50003; } message M { double p = 1 [(units) = "m\"bar", (range).max = 80, ', ...
  '(p.q.range) = { min: -1 max: 2 }, (lvl) = HIGH, deprecated = true, (nope) = 3, (tags) = 7]; }'], ...
  'Format', 'text');
o = protobuf.fields(typed, 'M').Options{1};
asserts.isequal(o(1).Value, string('m"bar'));
asserts.isequal(o(1).Decoded, 'm"bar');
asserts.isequal(o(2).Decoded, 80);
asserts.isequal(o(3).Value, "{ min : -1 max : 2 }");
asserts.isequal(o(3).Decoded, struct('min', -1, 'max', 2));
asserts.isequal(o(4).Decoded, int32(2));
asserts.isequal(o(5).Decoded, true);
asserts.isequal(o(6).Decoded, []);
asserts.isequal(o(7).Decoded, int32(7));
%=============================================================================
