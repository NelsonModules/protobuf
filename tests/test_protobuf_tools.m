%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% protobuf.show, protobuf.diff and protobuf.infer.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
s = protobuf.read(sch, 'SensorSample', [DATA, '/golden/sensor_stream.nelson.test.sensors.SensorSample.binpb'], ...
  'Delimited', true);
%=============================================================================
% show: one text block per record.
txt = evalc('protobuf.show(sch, ''SensorSample'', s, ''MaxRecords'', 2)');
asserts.istrue(startsWith(txt, sprintf('# record 1\n%s', protobuf.totext(sch, 'SensorSample', s(1)))));
asserts.istrue(endsWith(txt, sprintf('# ... 23 more records\n')));
txt = evalc('protobuf.show(sch, ''SensorSample'', protobuf.encode(sch, ''SensorSample'', s(1)))');
asserts.isequal(txt, protobuf.totext(sch, 'SensorSample', s(1)));
asserts.isequal(evalc('protobuf.show(sch, ''SensorSample'', struct())'), sprintf('(empty message)\n'));
%=============================================================================
% diff: through the schema, absent fields and defaults compare alike.
a = s(2);
b = a;
b.status = int32(99);
b.pressure = a.pressure;
T = protobuf.diff(sch, 'SensorSample', a, protobuf.encode(sch, 'SensorSample', b));
asserts.isequal(T.Path, "status");
asserts.isequal(T.Change, "added");
asserts.isequal(T.B{1}, int32(99));
asserts.isequal(height(protobuf.diff(sch, 'SensorSample', a, a)), 0);
T = protobuf.diff(struct('x', 1, 'y', {{1, 2}}, 'p', struct('q', [1 2])), ...
  struct('x', 2, 'z', 'k', 'p', struct('q', [1 3])));
asserts.isequal(T.Path, ["x"; "y"; "p.q"; "z"]);
asserts.isequal(T.Change, ["changed"; "removed"; "changed"; "added"]);
T = protobuf.diff(struct('r', struct('v', {1, 2})), struct('r', struct('v', {1, 5, 6})));
asserts.isequal(T.Path, ["r(2).v"; "r(3)"]);
asserts.isequal(T.Change, ["changed"; "added"]);
asserts.checkerror('protobuf.diff(1, 2)', 'protobuf.diff compares structs (or bytes with a schema).', ...
  'protobuf:invalidValue:protobufDiffComparesStructsBytes');
%=============================================================================
% infer: a schema that reads the data back.
v = struct('id', {int64(1), int64(2)}, 'name', {'a', ''}, 'tags', {{'x', 'y'}, {}}, ...
  'raw', {uint8([1 2]), uint8(3)}, 'when', {datetime(2026, 1, 1), datetime(2026, 1, 2)}, ...
  'pos', {struct('x', single(1)), struct('x', single(3))}, 'samples', {[1 2 3], 4}, 'ok', {true, false});
txt = protobuf.infer(v, 'Name', 'Measure', 'Package', 'demo');
expected = ['syntax = "proto3";', newline, newline, 'package demo;', newline, newline, ...
  'import "google/protobuf/timestamp.proto";', newline, newline, 'message Measure {', newline, ...
  '  message Pos {', newline, '    float x = 1;', newline, '  }', newline, ...
  '  int64 id = 1;', newline, '  optional string name = 2;', newline, '  repeated string tags = 3;', newline, ...
  '  bytes raw = 4;', newline, '  google.protobuf.Timestamp when = 5;', newline, '  Pos pos = 6;', newline, ...
  '  repeated double samples = 7;', newline, '  bool ok = 8;', newline, '}', newline];
asserts.isequal(txt, expected);
WORK = [tempdir(), createGUID()];
mkdir(WORK);
P = [WORK, '/measure.proto'];
fid = fopen(P, 'w');
fwrite(fid, txt);
fclose(fid);
q = protobuf.load(P);
back = protobuf.decode(q, 'Measure', protobuf.encode(q, 'Measure', v, 'Delimited', true), 'Delimited', true);
asserts.isequal(back(1).samples, [1 2 3]);
asserts.isequal(back(2).pos.x, single(3));
rmdir(WORK, 's');
T = table(int32([1; 2]), {'a'; 'b'}, [0.5; 1.5], 'VariableNames', {'k', 'label', 'value'});
asserts.isequal(protobuf.infer(T), ...
  sprintf('syntax = "proto3";\n\nmessage Record {\n  int32 k = 1;\n  string label = 2;\n  double value = 3;\n}\n'));
asserts.checkerror('protobuf.infer(3)', 'protobuf.infer expects a struct, a struct array or a table.', ...
  'protobuf:invalidValue:protobufInferExpectsStructStruct');
%=============================================================================
