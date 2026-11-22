%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% protobuf.Writer: records pushed to a file, a file identifier or a ZIP entry.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
WORK = [tempdir(), createGUID()];
mkdir(WORK);
%=============================================================================
% File name: structs and tables pushed one after the other.
F = [WORK, '/samples.pb'];
w = protobuf.Writer(sch, 'SensorSample', F);
asserts.istrue(w.IsOpen);
w.push(struct('timestamp_ms', {1, 2}, 'status', {3, 4}));
w.push(table(int64([5; 6]), int32([7; 8]), 'VariableNames', {'timestamp_ms', 'status'}));
asserts.isequal(w.Records, 4);
w.close();
asserts.isfalse(w.IsOpen);
asserts.checkerror('w.push(struct(''timestamp_ms'', 9))', 'The writer is closed.', 'protobuf:cannotWrite:writerClosed');
r = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(r.timestamp_ms, int64([1; 2; 5; 6]));
asserts.isequal(r.status, int32([3; 4; 7; 8]));
info = dir(F);
asserts.isequal(info.bytes, w.Bytes);
% Append adds after the existing records.
w = protobuf.Writer(sch, 'SensorSample', F, 'Append', true, 'Layout', 'columns');
w.push(struct('timestamp_ms', int64(10), 'status', int32(11)));
delete(w);
r = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(r.timestamp_ms, int64([1; 2; 5; 6; 10]));
%=============================================================================
% An open file identifier stays open.
fid = fopen(F, 'w');
w = protobuf.Writer(sch, 'SensorSample', fid);
w.push(struct('timestamp_ms', 1));
w.close();
fwrite(fid, protobuf.encode(sch, 'SensorSample', struct('timestamp_ms', 2), 'Delimited', true), 'uint8');
fclose(fid);
r = protobuf.read(sch, 'SensorSample', F, 'Delimited', true);
asserts.isequal([r.timestamp_ms], int64([1 2]));
%=============================================================================
% ZIP entry written as a stream.
Z = [WORK, '/samples.zip'];
zip = nelson.io.ZipArchive(Z, 'w');
w = protobuf.Writer(sch, 'SensorSample', zip, 'Entry', 'data/samples.pb', 'Layout', 'columns');
w.push(struct('timestamp_ms', int64([1; 2; 3]), 'status', int32([1; 2; 3])));
w.push(struct('timestamp_ms', int64(4), 'status', int32(4)));
asserts.isequal(w.Records, 4);
w.close();
zip.close();
zip = nelson.io.ZipArchive(Z);
r = protobuf.read(sch, 'SensorSample', zip, 'Entry', 'data/samples.pb', 'Delimited', true, 'Layout', 'columns');
asserts.isequal(r.status, int32([1; 2; 3; 4]));
zip.close();
%=============================================================================
% Errors.
asserts.checkerror('protobuf.Writer(sch, ''SensorSample'', zip)', ...
  'Writing to a ZIP archive needs ''Entry'', the entry name.', 'protobuf:invalidOption:writingZIPArchiveNeedsEntry');
asserts.checkerror('protobuf.Writer(sch, ''SensorSample'', F, ''Layout'', ''table'')', ...
  'Layout must be one of: ''struct'', ''columns''.', 'protobuf:invalidOption:layoutStructColumns');
rmdir(WORK, 's');
%=============================================================================
