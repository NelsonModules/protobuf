%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% proto2 files: labels, required fields, defaults, closed enums, groups, packed option.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/legacy_proto2.proto']);
%=============================================================================
% Field table: presence everywhere, defaults (the first value for an enum), required.
F = protobuf.fields(sch, 'Setting');
asserts.isequal(F.Name(1:8), ["id"; "label"; "value"; "unit"; "history"; "packed_history"; "range"; "point"]);
asserts.isequal(F.Type(F.Name == "range"), "message");
asserts.isequal(F.TypeName(F.Name == "range"), "nelson.test.legacy.Setting.Range");
asserts.istrue(F.Required(F.Name == "id"));
asserts.istrue(all(F.Presence(~F.Repeated)));
asserts.isequal(F.Default(F.Name == "label"), "none");
asserts.isequal(F.Default(F.Name == "unit"), "UNIT_CMH2O");
asserts.isequal(F.Default(F.Name == "raw"), "\001\002");
%=============================================================================
% Groups: start and end tags on the wire, the field named in lower case.
s = struct('id', 1, 'range', struct('low', 0.5), 'point', struct('at', {int64(1), int64(2)}), ...
  'manual', struct('operator_name', 'me'));
b = protobuf.encode(sch, 'Setting', s);
asserts.isequal(b(1:4), uint8([8 1 59 9]));
d = protobuf.decode(sch, 'Setting', b);
asserts.isequal(d.range.low, 0.5);
asserts.isequal([d.point.at], int64([1 2]));
asserts.isequal(d.source, 'manual');
asserts.isequal(d.manual.operator_name, 'me');
% The text format names a group by its type; both names are read back.
txt = protobuf.totext(sch, 'Setting', b);
asserts.istrue(contains(txt, sprintf('Range {\n  low: 0.5\n}')));
asserts.istrue(contains(txt, 'Point {'));
asserts.istrue(contains(txt, 'Manual {'));
asserts.isequal(protobuf.encode(sch, 'Setting', protobuf.fromtext(sch, 'Setting', txt)), b);
r = protobuf.fromtext(sch, 'Setting', 'id: 2 range { high: 3 }');
asserts.isequal(r.range.high, 3);
% JSON uses the field name.
asserts.istrue(contains(protobuf.tojson(sch, 'Setting', b), '"range":{"low":0.5}'));
%=============================================================================
% Repeated scalars are not packed unless asked.
b = protobuf.encode(sch, 'Setting', struct('id', 1, 'history', int32([1 2]), 'packed_history', int32([1 2])));
asserts.isequal(b, uint8([8 1 40 1 40 2 50 2 2 4]));
d = protobuf.decode(sch, 'Setting', uint8([8 1 42 2 1 2 48 2]));
asserts.isequal(d.history, int32([1 2]));
asserts.isequal(d.packed_history, int32(1));
%=============================================================================
% Closed enum: a number that is not a value is an unknown field.
d = protobuf.decode(sch, 'Setting', uint8([8 1 32 9 112 1 112 7]), 'KeepUnknown', true);
asserts.isequal(d.unit, []);
asserts.isequal(d.units, int32(1));
asserts.isequal(d.UnknownFields, uint8([32 9 112 7]));
%=============================================================================
% Required fields, also inside a repeated group.
asserts.checkerror('protobuf.encode(sch, ''Setting'', struct(''label'', ''x''))', ...
  'required field "id" of "nelson.test.legacy.Setting" is missing.', 'protobuf:encodeError:requiredFieldMissing');
asserts.checkerror('protobuf.encode(sch, ''Setting'', struct(''id'', 1, ''point'', struct(''y'', single(1))))', ...
  'required field "at" of "nelson.test.legacy.Setting.Point" is missing.', 'protobuf:encodeError:requiredFieldMissing');
% Decoding does not check them.
d = protobuf.decode(sch, 'Setting', uint8([18 1 120]));
asserts.isequal(d.id, []);
asserts.isequal(d.label, 'x');
%=============================================================================
% Strings are not checked as UTF-8 in proto2.
d = protobuf.decode(sch, 'Setting', uint8([8 1 18 1 255]));
asserts.isequal(double(d.label), 255);
%=============================================================================
% Descriptor set written and read back.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
fid = fopen([WORK, '/legacy.binpb'], 'w');
fwrite(fid, protobuf.descriptor(sch), 'uint8');
fclose(fid);
again = protobuf.load([WORK, '/legacy.binpb']);
asserts.isequal(__protobuf_dump__(again.Id), __protobuf_dump__(sch.Id));
% A proto3 file cannot use a closed proto2 enum with implicit presence.
fid = fopen([WORK, '/p2.proto'], 'w');
fwrite(fid, 'syntax = "proto2"; enum E { A = 1; }');
fclose(fid);
try
  protobuf.load('syntax = "proto3"; import "p2.proto"; message M { E e = 1; }', 'Format', 'text', ...
    'ImportPaths', WORK);
  asserts.istrue(false);
catch err
  asserts.istrue(contains(err.message, 'fields with implicit presence must use open enums.'));
end
% A file without syntax statement is proto2.
sch2 = protobuf.load('message A { optional int32 a = 1 [default = 7]; }', 'Format', 'text');
asserts.isequal(protobuf.fields(sch2, 'A').Default, "7");
rmdir(WORK, 's');
%=============================================================================
% FillDefaults: absent fields read as their default value; presence still tells them.
d = protobuf.decode(sch, 'Setting', uint8([8 1]), 'FillDefaults', true);
asserts.isequal(d.label, 'none');
asserts.isequal(d.value, 1.5);
asserts.isequal(d.unit, int32(2));
asserts.isequal(d.raw, uint8([1 2]));
asserts.isequal(d.enabled, true);
asserts.isequal(d.device, '');
[C, P] = protobuf.decode(sch, 'Setting', protobuf.encode(sch, 'Setting', struct('id', {1, 2}, 'value', {[], 4}), ...
  'Delimited', true), 'Delimited', true, 'Layout', 'columns', 'FillDefaults', true);
asserts.isequal(C.value, [1.5; 4]);
asserts.isequal(P.value, [false; true]);
asserts.isequal(C.label, {'none'; 'none'});
asserts.isequal(P.label, [false; false]);
%=============================================================================
