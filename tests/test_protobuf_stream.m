%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Writing and reading records in several steps.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
fid = fopen([DATA, '/golden/sensor_stream.nelson.test.sensors.Event.binpb'], 'r');
raw = fread(fid, Inf, '*uint8')';
fclose(fid);
whole = protobuf.decode(sch, 'Event', raw, 'Delimited', true);
%=============================================================================
% Pieces of any size give the records of the whole stream.
for piece = [1, 5, 64, numel(raw)]
  d = protobuf.Decoder(sch, 'Event');
  got = [];
  for first = 1:piece:numel(raw)
    d.push(raw(first:min(first + piece - 1, end)));
    got = [got, d.read()]; %#ok<AGROW>
  end
  asserts.isequal(protobuf.encode(sch, 'Event', got, 'Delimited', true), raw);
end
asserts.isequal(numel(got), numel(whole));
%=============================================================================
% read returns the new records only; a cut record waits for the next push.
d = protobuf.Decoder(sch, 'nelson.test.sensors.Event', 'Layout', 'columns');
cut = 40;
[n, skipped] = d.push(raw(1:cut));
[C, ~, report] = d.read();
asserts.isequal(skipped, 0);
asserts.isequal(report.Records, n);
asserts.isequal(numel(C.kind), n);
asserts.istrue(report.PendingBytes > 0);
d.push(raw(cut + 1:end));
[C2, ~, report2] = d.read();
asserts.isequal(report2.PendingBytes, 0);
asserts.isequal([C.kind; C2.kind], {whole.kind}');
[C3, ~, report3] = d.read();
asserts.isequal(report3.Records, 0);
asserts.isequal(size(C3.kind), [0 1]);
%=============================================================================
% Options and errors.
asserts.checkerror('protobuf.Decoder(sch, ''Event'', ''Delimited'', false)', ...
  'A decoder reads length-delimited records only.', 'protobuf:invalidOption:decoderReadsLengthDelimitedRecords');
asserts.checkerror('protobuf.Decoder(sch, ''Nope'')', 'unknown message type "Nope".', ...
  'protobuf:parseError:unknownMessageType');
d = protobuf.Decoder(sch, 'SensorSample');
asserts.checkerror('d.push([1 2])', 'The encoded data must be a uint8 vector.', ...
  'protobuf:invalidBytes:encodedDataUint8Vector');
bad = uint8([2 0 1, 3 8 150 1]);
asserts.checkerror('d.push(bad)', 'record 1: invalid field number in the input.', ...
  'protobuf:decodeError:invalidFieldNumberInput');
d = protobuf.Decoder(sch, 'SensorSample', 'OnError', 'skip');
[n, skipped] = d.push(bad);
asserts.isequal([n, skipped], [1, 1]);
asserts.isequal(d.read().timestamp_ms, int64(150));
%=============================================================================
% Writing in several calls: 'Append' on a file name, or an open file identifier.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
F = [WORK, '/records.bin'];
protobuf.write(sch, 'SensorSample', F, struct('timestamp_ms', {1, 2}), 'Delimited', true);
protobuf.write(sch, 'SensorSample', F, struct('timestamp_ms', 3), 'Delimited', true, 'Append', true);
fid = fopen(F, 'a');
protobuf.write(sch, 'SensorSample', fid, struct('timestamp_ms', {4, 5}), 'Delimited', true);
fclose(fid);
C = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(C.timestamp_ms, int64((1:5)'));
protobuf.write(sch, 'SensorSample', F, struct('timestamp_ms', 9), 'Delimited', true);
C = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(C.timestamp_ms, int64(9));
% ZIP entries: an archive with 'Entry', or a file identifier on an entry.
Z = [WORK, '/records.zip'];
z = nelson.io.zip.open(Z, 'w');
protobuf.write(sch, 'SensorSample', z, struct('timestamp_ms', {7, 8}), 'Delimited', true, 'Entry', 'data/samples.bin');
nelson.io.zip.close(z);
z = nelson.io.zip.open(Z);
C = protobuf.read(sch, 'SensorSample', z, 'Entry', 'data/samples.bin', 'Delimited', true, 'Layout', 'columns');
asserts.isequal(C.timestamp_ms, int64([7; 8]));
fid = nelson.io.zip.fopen(z, 'data/samples.bin');
C = protobuf.read(sch, 'SensorSample', fid, 'Delimited', true, 'Layout', 'columns');
fclose(fid);
asserts.isequal(C.timestamp_ms, int64([7; 8]));
asserts.checkerror('protobuf.read(sch, ''SensorSample'', z, ''Delimited'', true)', ...
  'Reading from a ZIP archive needs ''Entry'', the entry name.', 'protobuf:invalidOption:readingZIPArchiveNeedsEntry');
nelson.io.zip.close(z);
rmdir(WORK, 's');
%=============================================================================
