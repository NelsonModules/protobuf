%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% .proto parser: errors with their position, name scopes, imports.
%=============================================================================
% One case per line: .proto text (\n for a new line), then the expected error text.
cases = readCases([modulepath('protobuf'), '/tests/data/parser_errors.tsv']);
for k = 1:size(cases, 1)
  checkParseError(strrep(cases{k, 1}, '\n', newline), cases{k, 2});
end
%=============================================================================
% Default values in the canonical text of the reference descriptor.
sch = protobuf.load(['edition = "2023"; message D { double a = 1 [default = 5]; float b = 2 [default = -inf]; ', ...
  'double c = 3 [default = nan]; int32 d = 4 [default = -0]; sint64 e = 5 [default = -0x10]; ', ...
  'float f = 6 [default = 0.1]; double g = 7 [default = 1e300]; bool h = 8 [default = false]; ', ...
  'bytes i = 9 [default = "\n\r\t\"\''\\\001x"]; string j = 10 [default = "é\U0001F642"]; ', ...
  'uint64 k = 11 [default = 18446744073709551615]; double l = 12 [default = inf]; float m = 13 [default = nan]; }'], ...
  'Format', 'text');
F = protobuf.fields(sch, 'D');
asserts.isequal(F.Default, ["5"; "-inf"; "nan"; "0"; "-16"; "0.1"; "1e+300"; "false"; ...
  "\n\r\t\""\'\\\001x"; string(native2unicode(uint8([195 169 240 159 153 130]), ...
    'UTF-8')); "18446744073709551615"; "inf"; "nan"]);
%=============================================================================
% Ambiguous trailing names.
sch = protobuf.load(['syntax = "proto3"; package p; message A { message In {} } ', ...
  'message B { message In {} }'], 'Format', ...
  'text');
asserts.checkerror('protobuf.fields(sch, ''In'')', 'message name "In" is ambiguous ("p.A.In", "p.B.In").', ...
  'protobuf:parseError:messageNameAmbiguous');
asserts.isequal(height(protobuf.fields(sch, 'A.In')), 0);
%=============================================================================
% Imports weak and option-only, nested local enums, and a rich schema written as a
% descriptor set and read back.
text = ['edition = "2024"; package rich; import weak "google/protobuf/empty.proto"; ', ...
  'import option "google/protobuf/cpp_features.proto"; option features.utf8_validation = NONE; ', ...
  'export message A { option features.json_format = LEGACY_BEST_EFFORT; local enum Kind { KIND_A = 0; } ', ...
  '  local message In { int32 x = 1; } ', ...
  '  oneof choice { option features.enforce_naming_style = STYLE_LEGACY; int32 n = 1; string s = 2; } ', ...
  '  string label = 3 [default = "a\n\"b\\\001"]; reserved 100 to max; reserved old; ', ...
  '  Kind kind = 4; In in = 5 [features.message_encoding = DELIMITED]; } ', ...
  'local enum Closed { option features.enum_type = CLOSED; CLOSED_B = 2; reserved 5 to 9; reserved CLOSED_OLD; }'];
sch = protobuf.load(text, 'Format', 'text', 'Name', 'rich.proto');
WORK = [tempdir(), createGUID()];
mkdir(WORK);
fid = fopen([WORK, '/rich.binpb'], 'w');
fwrite(fid, protobuf.descriptor(sch), 'uint8');
fclose(fid);
again = protobuf.load([WORK, '/rich.binpb']);
asserts.isequal(__protobuf_dump__(again.Id), __protobuf_dump__(sch.Id));
fid = fopen([WORK, '/empty.binpb'], 'w');
fclose(fid);
asserts.checkerror('protobuf.load([WORK, ''/empty.binpb''])', ...
  [WORK, '/empty.binpb: the descriptor set has no file.'], ...
  ...
  'protobuf:parseError:descriptorSetFile');
rmdir(WORK, 's');
%=============================================================================
% STYLE_LEGACY turns the naming checks off.
sch = protobuf.load(['edition = "2024"; option features.enforce_naming_style = STYLE_LEGACY; message foo_bar { ', ...
  'int32 BadName = 1; }'], ...
  'Format', 'text');
asserts.isequal(sch.Messages, {'foo_bar'});
%=============================================================================
% Scopes: inner names hide outer ones, a leading dot starts from the root.
sch = protobuf.load(['syntax = "proto3"; package p; message T {} ', ...
  'message A { message T {} T inner = 1; .p.T outer = 2; }'], 'Format', 'text');
F = protobuf.fields(sch, 'p.A');
asserts.isequal(F.TypeName, ["p.A.T"; "p.T"]);
asserts.isequal(sch.File, 'input.proto');
sch = protobuf.load('syntax = "proto3"; message A {}', 'Format', 'text', 'Name', 'named.proto');
asserts.isequal(sch.File, 'named.proto');
%=============================================================================
% Lexer: comments, string escapes and concatenation, numbers in all bases, options.
sch = protobuf.load(sprintf([char(65279), '// line comment\nsyntax = "proto3"; /* block\ncomment */\n', ...
  'option java_package = "com." ''x''; option (custom.opt).a = { a: 1 b: [1, 2] };\n', ...
  'enum E { option allow_alias = true; Z = 0; A = 0x1; B = 01; C = -2 [deprecated = true]; }\n', ...
  'message M { int32 a = 0x1F [deprecated = true, json_name = "alpha"]; option (m) = -inf; ;\n', ...
  '  reserved 100 to max, 7; reserved "old", ''older''; }\n', ...
  'service S { option (s) = 1; rpc Do (stream M) returns (stream M) { option (r) = 2; } ', ...
  'rpc One (M) returns (M); }\n']), ...
  'Format', 'text');
F = protobuf.fields(sch, 'M');
asserts.isequal(F.Number, 31);
asserts.isequal(F.JsonName, "alpha");
d = __protobuf_dump__(sch.Id);
asserts.istrue(contains(d, 'reserved_range 100 536870912'));
asserts.istrue(contains(d, 'reserved_name older'));
asserts.istrue(contains(d, 'method Do .M .M 1 1'));
asserts.istrue(contains(d, 'value C -2'));
%=============================================================================
% Imports: folders in order, built-in well-known types, public imports, cycles.
WORK = [tempdir(), createGUID()];
mkdir([WORK, '/lib/shapes']);
writeText([WORK, '/lib/shapes/point.proto'], ...
  sprintf('syntax = "proto3";\npackage shapes;\nmessage Point { double x = 1; double y = 2; }\n'));
writeText([WORK, '/lib/shapes/all.proto'], sprintf('syntax = "proto3";\nimport public "shapes/point.proto";\n'));
writeText([WORK, '/main.proto'], sprintf(['syntax = "proto3";\npackage app;\nimport "shapes/all.proto";\n', ...
  'import "google/protobuf/timestamp.proto";\n', ...
  'message Track { repeated shapes.Point points = 1; google.protobuf.Timestamp at = 2; }\n']));
sch = protobuf.load([WORK, '/main.proto'], 'ImportPaths', [WORK, '/lib']);
asserts.isequal(sch.Imports, {'shapes/all.proto'; 'google/protobuf/timestamp.proto'});
F = protobuf.fields(sch, 'app.Track');
asserts.isequal(F.TypeName, ["shapes.Point"; "google.protobuf.Timestamp"]);
% The same imports from a text.
sch = protobuf.load(fileread([WORK, '/main.proto']), 'Format', 'text', 'ImportPaths', [WORK, '/lib']);
asserts.isequal(protobuf.fields(sch, 'app.Track').TypeName, F.TypeName);
try
  protobuf.load([WORK, '/main.proto']);
  asserts.istrue(false);
catch e
  asserts.istrue(startsWith(e.identifier, 'protobuf:parseError:'));
  asserts.istrue(endsWith(e.message, 'main.proto:3:8: import "shapes/all.proto" not found.'));
end
writeText([WORK, '/a.proto'], sprintf('syntax = "proto3";\nimport "b.proto";\n'));
writeText([WORK, '/b.proto'], sprintf('syntax = "proto3";\nimport "a.proto";\n'));
try
  protobuf.load([WORK, '/a.proto']);
  asserts.istrue(false);
catch e
  asserts.istrue(contains(e.message, 'import cycle: "a.proto" imports itself through its imports.'));
end
% Edition 2024 visibility: local types stay in their file.
writeText([WORK, '/vis.proto'], ['edition = "2024"; package lib; message Top { message Inner {} } ', ...
  'local message Hidden {} export message Shown { export message Nested {} }']);
sch = protobuf.load('edition = "2024"; import "vis.proto"; message Use { lib.Top a = 1; lib.Shown.Nested b = 2; }', ...
  'Format', 'text', 'ImportPaths', WORK);
asserts.isequal(numel(sch.Messages), 6);
checkParseError('edition = "2024"; import "vis.proto"; message Use { lib.Top.Inner a = 1; }', ...
  '"lib.Top.Inner" is local to "vis.proto" and cannot be used from another file.', WORK);
checkParseError('syntax = "proto3"; import "vis.proto"; message Use { lib.Hidden a = 1; }', ...
  '"lib.Hidden" is local to "vis.proto" and cannot be used from another file.', WORK);
rmdir(WORK, 's');
%=============================================================================
% load errors.
asserts.checkerror('protobuf.load(1)', 'The file name must be a text scalar.', ...
  'protobuf:invalidFilename:fileNameTextScalar');
asserts.checkerror('protobuf.load(''x'', ''Format'', ''json'')', ...
  'Format must be one of: ''auto'', ''proto'', ''descriptor'', ''text''.', ...
    'protobuf:invalidOption:formatAutoProtoDescriptorText');
asserts.checkerror('protobuf.load(''x'', ''Format'', ''text'', ''ImportPaths'', 1)', ...
  'ImportPaths must be text: a folder or a list of folders.', ...
    'protobuf:invalidOption:importpathsTextFolderListFolders');
%=============================================================================
function checkParseError(text, expected, importPaths)
  % The parser refuses text with a message containing expected.
  if nargin < 3
    importPaths = {};
  end
  try
    protobuf.load(text, 'Format', 'text', 'Name', 'x.proto', 'ImportPaths', importPaths);
  catch e
    if ~startsWith(e.identifier, 'protobuf:parseError:') || ~contains(e.message, expected)
      error('test:parser', 'For [%s]\n  expected: %s\n  got: %s (%s)', text, expected, e.message, e.identifier);
    end
    return
  end
  error('test:parser', 'For [%s] no error, expected: %s', text, expected);
end
%=============================================================================
function cases = readCases(filename)
  % Tab-separated cases of a data file; lines starting with # are comments.
  lines = strsplit(fileread(filename), newline);
  lines = lines(~cellfun(@isempty, lines) & ~startsWith(lines, '#'));
  cases = cell(numel(lines), 2);
  for k = 1:numel(lines)
    cases(k, :) = strsplit(lines{k}, sprintf('\t'), 'CollapseDelimiters', false);
  end
end
%=============================================================================
function writeText(filename, text)
  fid = fopen(filename, 'w');
  fwrite(fid, text);
  fclose(fid);
end
%=============================================================================
