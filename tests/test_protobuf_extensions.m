%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Extensions: extension ranges, extend blocks at file and message level, groups, custom options.
%=============================================================================
TEXT = ['syntax = "proto2"; package nelson.test.ext; ', ...
  'message Base { optional int32 id = 1; extensions 100 to 199, 500 to max; } ', ...
  'extend Base { optional string note = 100 [default = "none"]; repeated int32 marks = 101 [packed = true]; ', ...
  '  optional group Extra = 102 { optional double level = 1; } } ', ...
  'message Holder { optional int32 key = 1; extend Base { optional Holder holder = 150; optional int64 id = 500; } }'];
sch = protobuf.load(TEXT, 'Format', 'text');
%=============================================================================
% Extensions are members of the extended message; a taken short name becomes the full name.
F = protobuf.fields(sch, 'Base');
asserts.isequal(F.Name, ["id"; "note"; "marks"; "extra"; "holder"; "nelson_test_ext_Holder_id"]);
asserts.isequal(F.Extension, [""; "nelson.test.ext.note"; "nelson.test.ext.marks"; ...
  "nelson.test.ext.extra"; "nelson.test.ext.Holder.holder"; "nelson.test.ext.Holder.id"]);
asserts.isequal(F.Number, [1; 100; 101; 102; 150; 500]);
asserts.isequal(F.Default(2), "none");
asserts.istrue(all(F.Presence([2 4 5 6])));
%=============================================================================
% Wire format: like fields of the message (packed list, group tags).
s = struct('id', 1, 'note', 'hi', 'marks', int32([1 2]), 'extra', struct('level', 0.5), ...
  'holder', struct('key', 7), 'nelson_test_ext_Holder_id', int64(9));
b = protobuf.encode(sch, 'Base', s);
asserts.isequal(b(1:12), uint8([8 1 162 6 2 104 105 170 6 2 1 2]));
d = protobuf.decode(sch, 'Base', b);
asserts.isequal(d.note, 'hi');
asserts.isequal(d.marks, int32([1 2]));
asserts.isequal(d.extra.level, 0.5);
asserts.isequal(d.holder.key, int32(7));
asserts.isequal(d.nelson_test_ext_Holder_id, int64(9));
% An absent extension is [], or its default with FillDefaults.
d = protobuf.decode(sch, 'Base', uint8([8 1]));
asserts.isequal(d.note, []);
d = protobuf.decode(sch, 'Base', uint8([8 1]), 'FillDefaults', true);
asserts.isequal(d.note, 'none');
% A message without the extension keeps it as an unknown field.
plain = protobuf.load(['syntax = "proto2"; package nelson.test.ext; ', ...
  'message Base { optional int32 id = 1; extensions 100 to 199; }'], 'Format', 'text');
d = protobuf.decode(plain, 'Base', b(1:7), 'KeepUnknown', true);
asserts.isequal(d.UnknownFields, uint8([162 6 2 104 105]));
%=============================================================================
% Text format and JSON name extensions by their full name in brackets.
txt = protobuf.totext(sch, 'Base', b);
asserts.istrue(contains(txt, '[nelson.test.ext.note]: "hi"'));
asserts.istrue(contains(txt, '[nelson.test.ext.marks]: 2'));
asserts.istrue(contains(txt, sprintf('[nelson.test.ext.extra] {\n  level: 0.5\n}')));
asserts.istrue(contains(txt, '[nelson.test.ext.Holder.id]: 9'));
asserts.isequal(protobuf.encode(sch, 'Base', protobuf.fromtext(sch, 'Base', txt)), b);
r = protobuf.fromtext(sch, 'Base', 'id: 2 [ .nelson.test.ext.note ]: "x"');
asserts.isequal(r.note, 'x');
try
  protobuf.fromtext(sch, 'Base', '[nelson.test.ext.nope]: 1');
  error('test:noError', 'unknown extension accepted');
catch err
  asserts.istrue(contains(err.message, ...
    'unknown extension "nelson.test.ext.nope" for message "nelson.test.ext.Base"'), err.message);
end
js = protobuf.tojson(sch, 'Base', b);
asserts.istrue(contains(js, '"[nelson.test.ext.note]":"hi"'));
asserts.istrue(contains(js, '"[nelson.test.ext.Holder.holder]":{"key":7}'));
asserts.isequal(protobuf.encode(sch, 'Base', protobuf.fromjson(sch, 'Base', js)), b);
%=============================================================================
% Descriptor set written and read back; the .proto text keeps the extend blocks.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
SET = [WORK, '/ext.binpb'];
fid = fopen(SET, 'w');
fwrite(fid, protobuf.descriptor(sch), 'uint8');
fclose(fid);
again = protobuf.load(SET);
asserts.isequal(__protobuf_dump__(again.Id), __protobuf_dump__(sch.Id));
dump = __protobuf_dump__(sch.Id);
asserts.istrue(contains(dump, '  extension_range 100 200'));
asserts.istrue(contains(dump, 'extension note 100 1 9 - .nelson.test.ext.Base note - 0 -'));
rmdir(WORK, 's');
%=============================================================================
% proto3: extensions define custom options (google/protobuf/descriptor.proto is built in).
opt = protobuf.load(['syntax = "proto3"; package nelson.test.opt; import "google/protobuf/descriptor.proto"; ', ...
  'extend google.protobuf.FieldOptions { string units = 50000; optional int32 level = 50001; } ', ...
  'message M { double p = 1 [(units) = "mbar"]; }'], 'Format', 'text');
F = protobuf.fields(opt, 'google.protobuf.FieldOptions');
asserts.isequal(F.Extension(end - 1:end), ["nelson.test.opt.units"; "nelson.test.opt.level"]);
asserts.isequal(protobuf.fields(opt, 'M').Options{1}.Value, "mbar");
% Options messages decode with their extensions.
o = protobuf.decode(opt, 'google.protobuf.FieldOptions', uint8([130 181 24 4 109 98 97 114]));
asserts.isequal(o.units, 'mbar');
%=============================================================================
% Errors.
bad = @(t) protobuf.load(t, 'Format', 'text');
P2 = 'syntax = "proto2"; message A { optional int32 a = 1; extensions 10 to 20; } ';
cases = {
  [P2, 'extend A { optional int32 b = 30; }'], '"A" does not declare 30 as an extension number.'
  [P2, 'extend A { optional int32 b = 10; optional int32 c = 10; }'], ...
    'extension number 10 of "A" is already used by "b".'
  [P2, 'extend A { required int32 b = 10; }'], 'extension "b" cannot be required.'
  [P2, 'extend A { map<int32, int32> b = 10; }'], 'map fields are not allowed in extend blocks.'
  [P2, 'extend A { oneof o { int32 b = 10; } }'], 'oneofs are not allowed in extend blocks.'
  [P2, 'extend A { optional int32 b = 10 [json_name = "x"]; }'], 'json_name is not allowed on extensions.'
  [P2, 'extend a { optional int32 b = 10; }'], '"a" is not defined.'
  [P2, 'enum E { X = 1; } extend E { optional int32 b = 10; }'], '"E" is not a message type.'
  'syntax = "proto2"; message A { optional int32 a = 15; extensions 10 to 20; }', ...
    'extension range 10 to 20 includes field "a" (15).'
  'syntax = "proto2"; message A { extensions 10 to 20; extensions 15 to 30; }', ...
    'extension range 15 to 30 overlaps another extension range.'
  'syntax = "proto2"; message A { reserved 12; extensions 10 to 20; }', ...
    'extension range 10 to 20 overlaps a reserved range.'
  'syntax = "proto2"; message A { extensions 0 to 5; }', 'extension numbers must be between 1 and 536870911.'
  'syntax = "proto2"; message A { extensions 9 to 5; }', 'extension range end number must be greater than start number.'
  'syntax = "proto3"; message A { extensions 10 to 20; }', 'extension ranges are not allowed in proto3.'
  'syntax = "proto2"; message A { option message_set_wire_format = true; extensions 4 to max; }', ...
    'message_set_wire_format is not supported.'
};
for k = 1:size(cases, 1)
  try
    bad(cases{k, 1});
    error('test:noError', 'no error for: %s', cases{k, 1});
  catch err
    asserts.istrue(endsWith(err.message, cases{k, 2}), err.message);
  end
end
% proto3 extends only options messages.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
fid = fopen([WORK, '/e2.proto'], 'w');
fwrite(fid, [P2, 'extend A { optional int32 z = 20; }']);
fclose(fid);
try
  protobuf.load('syntax = "proto3"; import "e2.proto"; extend A { int32 b = 11; }', 'Format', 'text', ...
    'ImportPaths', WORK);
  error('test:noError', 'proto3 extension of a plain message accepted');
catch err
  asserts.istrue(endsWith(err.message, 'extensions in proto3 are only allowed for defining options.'), err.message);
end
% An extension of an imported file.
x = protobuf.load('syntax = "proto2"; import "e2.proto"; extend A { optional string w = 11; }', ...
  'Format', 'text', 'ImportPaths', WORK);
asserts.isequal(protobuf.fields(x, 'A').Name, ["a"; "z"; "w"]);
rmdir(WORK, 's');
%=============================================================================
% Editions: extension ranges with declarations; features in aggregate form.
e = protobuf.load(['edition = "2023"; option features = { field_presence: IMPLICIT enum_type: CLOSED }; ', ...
  'message A { int32 a = 1; extensions 10 to max [declaration = { number: 10 full_name: ".b" type: "int32" }]; } ', ...
  'extend A { int32 b = 10; }'], 'Format', 'text');
F = protobuf.fields(e, 'A');
asserts.isequal(F.Presence, [false; true]);
%=============================================================================
% deprecated_legacy_json_field_conflicts allows JSON name conflicts.
try
  protobuf.load('syntax = "proto3"; message A { int32 a_b = 1; int32 aB = 2; }', 'Format', 'text');
  error('test:noError', 'JSON name conflict accepted');
catch err
  asserts.istrue(endsWith(err.message, 'the JSON name of field "aB" conflicts with field "a_b".'), err.message);
end
protobuf.load(['syntax = "proto3"; message A { option deprecated_legacy_json_field_conflicts = true; ', ...
  'int32 a_b = 1; int32 aB = 2; }'], 'Format', 'text');
%=============================================================================
