%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Imports and well-known types (Timestamp, Duration, wrappers).
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/well_known.proto']);
asserts.isequal(sch.Imports, {'google/protobuf/timestamp.proto'; 'google/protobuf/duration.proto'; ...
  'google/protobuf/wrappers.proto'});
asserts.istrue(any(strcmp(sch.Messages, 'google.protobuf.Timestamp')));
t = datetime(2026, 11, 12, 10, 30, 15.123456, 'TimeZone', 'UTC');
r = struct('at', t, 'elapsed', seconds(-90.25), 'level', 2.5, 'count', [], 'label', '', ...
  'marks', [t, t + hours(1)], 'samples', single([1 2 3]));
bytes = protobuf.encode(sch, 'Reading', r);
d = protobuf.decode(sch, 'Reading', bytes);
asserts.isequal(posixtime(d.at), posixtime(t));
asserts.isequal(d.at.TimeZone, 'UTC');
asserts.isequal(seconds(d.elapsed), -90.25);
asserts.isequal(d.level, 2.5);
asserts.isequal(d.count, []);
asserts.isequal(d.label, '');
asserts.isequal(posixtime(d.marks), posixtime([t, t + hours(1)]));
asserts.isequal(d.samples, single([1 2 3]));
% Raw messages without the conversion.
raw = protobuf.decode(sch, 'Reading', bytes, 'WellKnownTypes', false);
asserts.isequal(raw.at.seconds, int64(floor(posixtime(t))));
asserts.isequal(raw.at.nanos, int32(123456000));
asserts.isequal(raw.elapsed.seconds, int64(-90));
asserts.isequal(raw.elapsed.nanos, int32(-250000000));
asserts.isequal(raw.level.value, 2.5);
% The raw struct form is also accepted when encoding.
asserts.isequal(protobuf.encode(sch, 'Reading', raw), bytes);
%=============================================================================
% Columns: datetime and duration columns, wrappers as value columns with presence.
records = [r, struct('at', [], 'elapsed', [], 'level', [], 'count', 7, 'label', 'b', 'marks', [], 'samples', [])];
stream = protobuf.encode(sch, 'Reading', records, 'Delimited', true);
[C, P] = protobuf.decode(sch, 'Reading', stream, 'Delimited', true, 'Layout', 'columns');
asserts.istrue(isdatetime(C.at));
asserts.isequal(isnat(C.at), [false; true]);
asserts.isequal(P.at, [true; false]);
asserts.isequal(class(C.count), 'int32');
asserts.isequal(C.count, int32([0; 7]));
asserts.isequal(P.count, [false; true]);
asserts.isequal(protobuf.encode(sch, 'Reading', C, 'Layout', 'columns', 'Presence', P, 'Delimited', true), stream);
TT = protobuf.decode(sch, 'Reading', stream, 'Delimited', true, 'Layout', 'timetable', 'RowTimes', 'at', 'Records', ...
  [1 1]);
asserts.isequal(posixtime(TT.Properties.RowTimes), posixtime(t));
%=============================================================================
asserts.checkerror('protobuf.encode(sch, ''Reading'', struct(''at'', 3))', ...
  'field "at" expects a datetime value or a struct.', 'protobuf:invalidValue:fieldExpectsValueStruct');
local = datetime(2026, 1, 1, 'TimeZone', 'Europe/Paris');
asserts.checkerror('protobuf.encode(sch, ''Reading'', struct(''at'', local))', ...
  'field "at" expects UTC datetime values (or no time zone).', 'protobuf:invalidValue:fieldExpectsUTCDatetimeValues');
%=============================================================================
% Import paths: folders searched after the folder of the file.
WORK = [tempdir(), createGUID()];
mkdir([WORK, '/lib/shapes']);
filewrite([WORK, '/lib/shapes/point.proto'], ...
  sprintf('syntax = "proto3";\npackage shapes;\nmessage Point { double x = 1; double y = 2; }\n'));
filewrite([WORK, '/main.proto'], ...
  sprintf('syntax = "proto3";\nimport "shapes/point.proto";\nmessage Path { repeated shapes.Point points = 1; }\n'));
asserts.checkerror('protobuf.load([WORK, ''/main.proto''])', ...
  [WORK, '/main.proto:2:8: import "shapes/point.proto" not found.'], 'protobuf:parseError:importNotFound');
p = protobuf.load([WORK, '/main.proto'], 'ImportPaths', [WORK, '/lib']);
v = protobuf.decode(p, 'Path', protobuf.encode(p, 'Path', struct('points', struct('x', {1, 2}, 'y', 3))));
asserts.isequal([v.points.x], [1 2]);
rmdir(WORK, 's');
%=============================================================================
% Descriptor sets: one written by the reference compiler, and ours read back.
d = protobuf.load([DATA, '/well_known.binpb']);
asserts.isequal(d.Imports, sch.Imports);
asserts.isequal(sort(d.Messages), sort(sch.Messages));
asserts.isequal(protobuf.encode(d, 'Reading', r), bytes);
WORK = [tempdir(), createGUID()];
mkdir(WORK);
fid = fopen([WORK, '/set.desc'], 'w');
fwrite(fid, protobuf.descriptor(sch), 'uint8');
fclose(fid);
e = protobuf.load([WORK, '/set.desc']);
asserts.isequal(protobuf.fields(e, 'Reading'), protobuf.fields(sch, 'Reading'));
asserts.checkerror('protobuf.load([DATA, ''/well_known.proto''], ''Format'', ''descriptor'')', ...
  [DATA, ...
    '/well_known.proto: not a valid descriptor set (unsupported wire type in a descriptor.)'], ...
      'protobuf:parseError:unsupportedWireTypeDescriptor');
rmdir(WORK, 's');
%=============================================================================
