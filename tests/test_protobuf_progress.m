%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% 'Progress', @(n, total): called after each block of a length-delimited read.
%=============================================================================
sch = protobuf.load('syntax = "proto3"; message R { int64 t = 1; bytes payload = 2; }', 'Format', 'text');
% About 20 MiB: three blocks of 8 MiB at most.
N = 2600;
s = struct('t', num2cell(int64(1:N)), 'payload', {uint8(7) * ones(1, 8000, 'uint8')});
bytes = protobuf.encode(sch, 'R', s, 'Delimited', true);
total = numel(bytes);
WORK = [tempdir(), createGUID()];
mkdir(WORK);
FILE = [WORK, '/records.binpb'];
fid = fopen(FILE, 'w');
fwrite(fid, bytes, 'uint8');
fclose(fid);
%=============================================================================
% From a file: byte counts up to the file size, same records as without the callback.
global PB_PROGRESS
PB_PROGRESS = zeros(0, 2);
[C, P, report] = protobuf.read(sch, 'R', FILE, 'Delimited', true, 'Layout', 'columns', ...
  'Progress', @record);
rows = PB_PROGRESS;
asserts.isequal(size(rows), [3 2]);
asserts.isequal(rows(:, 2), repmat(total, 3, 1));
asserts.isequal(rows(:, 1), [8 * 1024 * 1024; 16 * 1024 * 1024; total]);
asserts.isequal(C.t, int64(1:N)');
asserts.isequal(report.Records, N);
reference = protobuf.read(sch, 'R', FILE, 'Delimited', true, 'Layout', 'columns');
asserts.isequal(C, reference);
%=============================================================================
% From bytes in memory, and with Where.
PB_PROGRESS = zeros(0, 2);
r = protobuf.decode(sch, 'R', bytes, 'Delimited', true, 'Progress', @record, ...
  'Where', @(c) mod(c.t, 1000) == 0);
asserts.isequal([r.t], int64([1000 2000]));
asserts.isequal(PB_PROGRESS, [8 * 1024 * 1024, total; 16 * 1024 * 1024, total; total, total]);
%=============================================================================
% From an open file: the bytes left after the current position.
fid = fopen(FILE, 'r');
fseek(fid, 0, 'bof');
PB_PROGRESS = zeros(0, 2);
r = protobuf.read(sch, 'R', fid, 'Delimited', true, 'Progress', @record);
fclose(fid);
asserts.isequal(numel(r), N);
rows = PB_PROGRESS;
asserts.isequal(rows(end, :), [total, total]);
% An empty file reports 0 of 0.
EMPTY = [WORK, '/empty.binpb'];
fclose(fopen(EMPTY, 'w'));
PB_PROGRESS = zeros(0, 2);
r = protobuf.read(sch, 'R', EMPTY, 'Delimited', true, 'Progress', @record);
asserts.isequal(numel(r), 0);
asserts.isequal(PB_PROGRESS, [0, 0]);
%=============================================================================
% Writing: bytes written in blocks of 8 MiB, to a file name or an open file identifier.
OUT = [WORK, '/written.binpb'];
PB_PROGRESS = zeros(0, 2);
protobuf.write(sch, 'R', OUT, s, 'Delimited', true, 'Progress', @record);
asserts.isequal(PB_PROGRESS, [8 * 1024 * 1024, total; 16 * 1024 * 1024, total; total, total]);
fid = fopen(OUT, 'r');
asserts.isequal(fread(fid, Inf, '*uint8')', bytes);
fclose(fid);
fid = fopen(OUT, 'w');
PB_PROGRESS = zeros(0, 2);
protobuf.write(sch, 'R', fid, s(1:2), 'Delimited', true, 'Progress', @record);
fclose(fid);
small = protobuf.encode(sch, 'R', s(1:2), 'Delimited', true);
asserts.isequal(PB_PROGRESS, [numel(small), numel(small)]);
% Writer: bytes written so far after each push; the total is not known.
PB_PROGRESS = zeros(0, 2);
w = protobuf.Writer(sch, 'R', OUT, 'Progress', @record);
w.push(s(1:2));
w.push(s(3));
w.close();
asserts.isequal(PB_PROGRESS(:, 1), [numel(small); w.Bytes]);
asserts.istrue(all(isnan(PB_PROGRESS(:, 2))));
% Index: bytes scanned in blocks of 64 MiB, and the same index as without the callback.
PB_PROGRESS = zeros(0, 2);
info = protobuf.index(FILE, 'Step', 100, 'Progress', @record);
asserts.isequal(PB_PROGRESS, [total, total]);
again = protobuf.index(FILE, 'Step', 100);
asserts.isequal(info, again);
asserts.isequal(info.Records, N);
% The scan in blocks (1 MB here) builds the same index file.
IDX = [WORK, '/blocks.pbidx'];
state = [];
blocks = 0;
infoBlocks = [];
while isempty(state) || ~state.Done
  [infoBlocks, state] = __protobuf_index__(FILE, IDX, 100, state, 1e6);
  blocks = blocks + 1;
end
asserts.istrue(blocks > 10);
asserts.isequal(infoBlocks, rmfield(info, {'File', 'IndexFile'}));
fid = fopen(IDX, 'r');
fromBlocks = fread(fid, Inf, '*uint8');
fclose(fid);
fid = fopen([FILE, '.pbidx'], 'r');
fromOne = fread(fid, Inf, '*uint8');
fclose(fid);
asserts.isequal(fromBlocks, fromOne);
rmdir(WORK, 's');
%=============================================================================
% Option checks.
asserts.checkerror('protobuf.decode(sch, ''R'', bytes, ''Progress'', @(n, t) 0)', ...
  'Progress needs ''Delimited'', true.', 'protobuf:invalidOption:progressNeedsDelimitedTrue');
asserts.checkerror('protobuf.decode(sch, ''R'', bytes, ''Delimited'', true, ''Progress'', 3)', ...
  'Progress must be a function handle called as f(n, total) with byte counts.', ...
    'protobuf:invalidOption:progressFunctionHandleCalledF');
cmd = 'protobuf.decode(sch, ''R'', bytes, ''Delimited'', true, ''Progress'', @(n, t) 0, ''Records'', 1:2)';
asserts.checkerror(cmd, ...
  'Progress and Records cannot be used together.', 'protobuf:invalidOption:progressRecordsUsedTogether');
asserts.checkerror('protobuf.write(sch, ''R'', [tempdir(), ''x.binpb''], s, ''Progress'', 1)', ...
  'Progress must be a function handle called as f(n, total) with byte counts.', ...
  'protobuf:invalidOption:progressFunctionHandleCalledF');
%=============================================================================
function record(n, t)
  global PB_PROGRESS
  PB_PROGRESS(end + 1, :) = [n, t];
end
%=============================================================================
