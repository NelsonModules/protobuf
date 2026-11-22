%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Files mapped in memory and record indexes (protobuf.index).
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
fid = fopen([DATA, '/golden/sensor_stream.nelson.test.sensors.SensorSample.binpb'], 'r');
one = fread(fid, Inf, '*uint8');
fclose(fid);
WORK = [tempdir(), createGUID()];
mkdir(WORK);
F = [WORK, '/samples.pb'];
fid = fopen(F, 'w');
for k = 1:40
  fwrite(fid, one, 'uint8');
end
fclose(fid);
whole = protobuf.decode(sch, 'SensorSample', repmat(one, 40, 1), 'Delimited', true);
%=============================================================================
% A mapped file decodes like its bytes.
asserts.isequal(protobuf.read(sch, 'SensorSample', F, 'Delimited', true), whole);
%=============================================================================
% Index: records found from the nearest indexed offset.
info = protobuf.index(F, 'Step', 7);
asserts.isequal(info.Records, 1000);
asserts.isequal(info.Step, 7);
asserts.isequal(info.Bytes, 40 * numel(one));
asserts.isequal(info.IndexFile, [info.File, '.pbidx']);
asserts.istrue(isfile([F, '.pbidx']));
part = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Records', [500 520]);
asserts.isequal(part, whole(500:520));
part = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Records', [995 Inf]);
asserts.isequal(part, whole(995:end));
% Another index file.
other = [WORK, '/other.idx'];
info = protobuf.index(F, 'IndexFile', other, 'Step', 100);
asserts.isequal(info.IndexFile, other);
part = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Records', [301 302], 'Index', other);
asserts.isequal(part, whole(301:302));
%=============================================================================
% An index written for another version of the file, or not an index, is not used.
fid = fopen(F, 'a');
fwrite(fid, one(1:20), 'uint8');
fclose(fid);
part = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Records', [999 1000]);
asserts.isequal(part, whole(999:1000));
info = protobuf.index(F);
asserts.isequal(info.Records, 1000);
asserts.isequal(info.Bytes, 40 * numel(one));
fid = fopen([F, '.pbidx'], 'w');
fwrite(fid, uint8(1:100), 'uint8');
fclose(fid);
part = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Records', [2 3], 'OnError', 'skip');
asserts.isequal(part, whole(2:3));
%=============================================================================
% Errors.
asserts.checkerror('protobuf.index(F, ''Step'', 0)', 'Step must be a positive integer.', ...
  'protobuf:invalidOption:stepPositiveInteger');
asserts.checkerror('protobuf.index([WORK, ''/missing.pb''])', ['File not found: ', WORK, '/missing.pb'], ...
  'protobuf:fileNotFound:noSuchFile');
asserts.checkerror('protobuf.read(sch, ''SensorSample'', [WORK, ''/missing.pb''])', ...
  ['File not found: ', WORK, '/missing.pb'], 'protobuf:fileNotFound:noSuchFile');
rmdir(WORK, 's');
%=============================================================================
