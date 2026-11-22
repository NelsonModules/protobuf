%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% protobuf.enums and the EnumsAsText decode option.
%=============================================================================
sch = protobuf.load(['syntax = "proto3"; package nelson.test.en; ', ...
  'enum Mode { MODE_OFF = 0; MODE_ON = 1; MODE_AUTO = 5; } ', ...
  'message M { Mode mode = 1; repeated Mode modes = 2; optional Mode opt = 3; int32 x = 4; ', ...
  '} message N { enum Mode { INNER_A = 0; } }'], 'Format', 'text');
%=============================================================================
% Enum types of a schema, then the values of one type.
asserts.isequal(protobuf.enums(sch), ["nelson.test.en.Mode"; "nelson.test.en.N.Mode"]);
T = protobuf.enums(sch, 'nelson.test.en.Mode');
asserts.isequal(T.Name, ["MODE_OFF"; "MODE_ON"; "MODE_AUTO"]);
asserts.isequal(T.Number, [0; 1; 5]);
T = protobuf.enums(sch, 'N.Mode');
asserts.isequal(T.Name, "INNER_A");
asserts.checkerror('protobuf.enums(sch, ''Mode'')', ...
  'enum name "Mode" is ambiguous ("nelson.test.en.Mode", "nelson.test.en.N.Mode").', ...
    'protobuf:invalidType:enumNameAmbiguous');
asserts.checkerror('protobuf.enums(sch, ''Nope'')', 'unknown enum type "Nope".', ...
  'protobuf:invalidType:unknownEnumType');
%=============================================================================
% Struct layout: names, the number as text when it has no name.
b = protobuf.encode(sch, 'M', struct('mode', 'MODE_AUTO', 'modes', int32([1 7 0]), 'x', 2));
d = protobuf.decode(sch, 'M', b, 'EnumsAsText', true);
asserts.isequal(d.mode, 'MODE_AUTO');
asserts.isequal(d.modes, {'MODE_ON', '7', 'MODE_OFF'});
asserts.isequal(d.opt, []);
asserts.isequal(d.x, int32(2));
% Without the option they stay numbers.
d0 = protobuf.decode(sch, 'M', b);
asserts.isequal(d0.mode, int32(5));
asserts.isequal(d0.modes, int32([1 7 0]));
% Names, unknown numbers as text included, encode back to the same bytes.
asserts.isequal(protobuf.encode(sch, 'M', d), b);
% Implicit presence: an absent value reads as the first name.
d = protobuf.decode(sch, 'M', uint8([]), 'EnumsAsText', true);
asserts.isequal(d.mode, 'MODE_OFF');
asserts.isequal(d.modes, cell(1, 0));
%=============================================================================
% Columns layout: cell columns of names; an absent optional value is ''.
s = struct('mode', {'MODE_ON', 'MODE_OFF'}, 'opt', {'MODE_AUTO', []}, 'modes', {{'MODE_ON'}, {}});
bytes = protobuf.encode(sch, 'M', s, 'Delimited', true);
[C, P] = protobuf.decode(sch, 'M', bytes, 'Delimited', true, 'Layout', 'columns', 'EnumsAsText', true);
asserts.isequal(C.mode, {'MODE_ON'; 'MODE_OFF'});
asserts.isequal(C.opt, {'MODE_AUTO'; ''});
asserts.isequal(P.opt, [true; false]);
asserts.isequal(C.modes, {{'MODE_ON'}; cell(1, 0)});
[C, P] = protobuf.decode(sch, 'M', bytes, 'Delimited', true, 'Layout', 'columns', ...
  'EnumsAsText', true, 'FillDefaults', true);
asserts.isequal(C.opt, {'MODE_AUTO'; 'MODE_OFF'});
asserts.isequal(P.opt, [true; false]);
%=============================================================================
% Text that is neither a name nor a number is refused.
asserts.checkerror('protobuf.encode(sch, ''M'', struct(''mode'', ''7x''))', ...
  '"7x" is not a value of enum "nelson.test.en.Mode".', 'protobuf:invalidValue:notAnEnumValue');
%=============================================================================
