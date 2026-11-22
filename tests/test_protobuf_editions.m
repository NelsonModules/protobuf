%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% .proto files written with editions 2023 and 2024.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/editions_2023.proto']);
%=============================================================================
% Singular fields have presence by default: an absent one reads as [].
r = protobuf.decode(sch, 'Reading', uint8([8 3]));
asserts.isequal(r.id, int32(3));
asserts.isequal(r.value, []);
asserts.isequal(r.count, int32(0));
F = protobuf.fields(sch, 'Reading');
asserts.isequal(F.Default(F.Name == "value"), "1.5");
asserts.isequal(F.Default(F.Name == "fallback"), "LEVEL_HIGH");
asserts.isequal(F.Default(F.Name == "level"), "LEVEL_LOW");
asserts.istrue(F.Required(F.Name == "id"));
asserts.isfalse(F.Presence(F.Name == "count"));
% With FillDefaults, they read as their default values, as a protobuf accessor gives them.
r = protobuf.decode(sch, 'Reading', uint8([8 3]), 'FillDefaults', true);
asserts.isequal(r.value, 1.5);
asserts.isequal(r.unit, 'bar');
asserts.isequal(r.tag, uint8([1 120]));
asserts.isequal(r.level, int32(1));
asserts.isequal(r.fallback, int32(5));
asserts.isequal(r.ratio, single(-Inf));
asserts.isequal(r.flag, true);
asserts.isequal(r.total, intmax('uint64'));
asserts.istrue(isempty(r.at));
%=============================================================================
% Delimited messages are groups on the wire.
b = protobuf.encode(sch, 'Reading', struct('id', 3, 'at', struct('x', 4)));
asserts.isequal(b, uint8([8 3 83 8 4 84]));
asserts.isequal(protobuf.decode(sch, 'Reading', b).at.x, int32(4));
asserts.isequal(protobuf.totext(sch, 'Reading', protobuf.decode(sch, 'Reading', b)), ...
  sprintf('id: 3\nat {\n  x: 4\n}\n'));
%=============================================================================
% A legacy required field must be set to encode.
asserts.checkerror('protobuf.encode(sch, ''Reading'', struct(''value'', 2))', ...
  'required field "id" of "nelson.test.editions.Reading" is missing.', 'protobuf:encodeError:requiredFieldMissing');
%=============================================================================
% A number outside a closed enum is an unknown field, not a value.
c = protobuf.decode(sch, 'Reading', uint8([8 3 40 7]), 'KeepUnknown', true);
asserts.isequal(c.level, []);
asserts.isequal(c.UnknownFields, uint8([40 7]));
%=============================================================================
% UTF-8: not checked in raw_text (utf8_validation = NONE), checked in the other strings.
r = protobuf.decode(sch, 'Reading', uint8([8 3 98 1 255]));
asserts.isequal(double(r.raw_text), 255);
asserts.checkerror('protobuf.decode(sch, ''Reading'', uint8([8 3 26 1 255]))', ...
  'string field "unit" contains invalid UTF-8.', 'protobuf:decodeError:stringFieldContainsInvalidUTF');
%=============================================================================
% Columns of the records written by the reference runtime.
[t, p] = protobuf.read(sch, 'Reading', [DATA, '/golden/editions_2023.nelson.test.editions.Reading.binpb'], ...
  'Layout', 'columns', 'Delimited', true);
asserts.isequal(numel(t.id), 25);
asserts.isequal(p.value(1:4), [false; false; false; true]);
%=============================================================================
% Edition 2024: file features and symbol visibility.
e24 = protobuf.load([DATA, '/editions_2024.proto']);
v = protobuf.decode(e24, 'Envelope', protobuf.encode(e24, 'Envelope', ...
  struct('label', 'x', 'body', struct('text', 'hi'))));
asserts.isequal(v.body.text, 'hi');
asserts.isequal(v.kind, []);
%=============================================================================
% Errors follow the edition rules.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
BAD = [WORK, '/bad.proto'];
fid = fopen(BAD, 'w');
fprintf(fid, 'edition = "2023";\nmessage B { optional int32 x = 1; }\n');
fclose(fid);
asserts.checkerror('protobuf.load(BAD)', [BAD, ':2:13: the optional label is not allowed in editions; ', ...
  'singular fields have presence unless features.field_presence = IMPLICIT.'], ...
  'protobuf:parseError:optionalLabelAllowedEditionsSingular');
rmdir(WORK, 's');
%=============================================================================
