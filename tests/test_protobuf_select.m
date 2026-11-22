%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Fields of sub-messages ('a.b') and the Where filter.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
F = [DATA, '/golden/sensor_stream.nelson.test.sensors.SensorSample.binpb'];
fid = fopen(F, 'r');
bytes = fread(fid, Inf, '*uint8');
fclose(fid);
whole = protobuf.decode(sch, 'SensorSample', bytes, 'Delimited', true);
cols = protobuf.decode(sch, 'SensorSample', bytes, 'Delimited', true, 'Layout', 'columns');
%=============================================================================
% Fields of a sub-message.
s = protobuf.decode(sch, 'SensorSample', bytes, 'Delimited', true, ...
  'Fields', {'timestamp_ms', 'diagnostics.drift'});
asserts.isequal(fieldnames(s), {'timestamp_ms'; 'diagnostics'});
k = find(arrayfun(@(r) ~isempty(r.diagnostics), whole), 1);
asserts.isequal(fieldnames(s(k).diagnostics), {'drift'});
asserts.isequal(s(k).diagnostics.drift, whole(k).diagnostics.drift);
c = protobuf.decode(sch, 'SensorSample', bytes, 'Delimited', true, 'Layout', 'columns', ...
  'Fields', {'status', 'diagnostics.drift', 'diagnostics.voltage'});
asserts.isequal(fieldnames(c.diagnostics), {'voltage'; 'drift'});
asserts.isequal(c.diagnostics.drift, cols.diagnostics.drift);
% A field named alone is kept whole.
c = protobuf.decode(sch, 'SensorSample', bytes, 'Delimited', true, 'Layout', 'columns', ...
  'Fields', {'diagnostics.drift', 'diagnostics'});
asserts.isequal(c.diagnostics, cols.diagnostics);
cmd = 'protobuf.decode(sch, ''SensorSample'', bytes, ''Delimited'', true, ''Fields'', ''status.x'')';
asserts.checkerror(cmd, ['field "status" of message "nelson.test.sensors.SensorSample" has no fields ', ...
  'to select (only singular and repeated message fields have).'], ...
  'protobuf:invalidOption:fieldMessageFieldsSelectSingular');
cmd = 'protobuf.decode(sch, ''SensorSample'', bytes, ''Delimited'', true, ''Fields'', ''diagnostics.nope'')';
asserts.checkerror(cmd, ...
  'unknown field "nope" for message "nelson.test.sensors.SensorSample.Diagnostics".', ...
  'protobuf:invalidOption:unknownFieldMessage');
%=============================================================================
% Where: records kept block by block, from a file, bytes or a decoder.
keep = cols.status > 0;
w = protobuf.read(sch, 'SensorSample', F, 'Delimited', true, 'Where', @(C) C.status > 0);
asserts.isequal(w, whole(keep));
[w, p, report] = protobuf.decode(sch, 'SensorSample', bytes, 'Delimited', true, ...
  'Layout', 'columns', 'Where', @(C) C.status > 0);
asserts.isequal(w.status, cols.status(keep));
asserts.isequal(w.diagnostics.drift, cols.diagnostics.drift(keep));
asserts.isequal(report.Records, nnz(keep));
T = protobuf.decode(sch, 'SensorSample', bytes, 'Delimited', true, 'Layout', 'table', ...
  'Where', @(C) C.timestamp_ms >= 0, 'Fields', {'timestamp_ms', 'status'});
asserts.isequal(T.Properties.VariableNames, {'timestamp_ms', 'status'});
asserts.isequal(T.status, cols.status(cols.timestamp_ms >= 0));
d = protobuf.Decoder(sch, 'SensorSample', 'Where', @(C) C.status > 0);
for first = 1:37:numel(bytes)
  d.push(bytes(first:min(first + 36, end)));
end
asserts.isequal(d.read(), whole(keep));
asserts.isequal(size(d.read()), [1 0]);
%=============================================================================
% Errors.
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', bytes, ''Where'', @(C) true)', ...
  'Where needs ''Delimited'', true.', 'protobuf:invalidOption:whereNeedsDelimitedTrue');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', bytes, ''Delimited'', true, ''Where'', 3)', ...
  'Where must be a function handle taking the columns of a block of records.', ...
    'protobuf:invalidOption:whereFunctionHandleTakingColumns');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', bytes, ''Delimited'', true, ''Where'', @(C) true)', ...
  'Where must return one logical value per record (25 records in this block).', ...
    'protobuf:invalidOption:whereReturnLogicalValuePer');
cmd = 'protobuf.decode(sch, ''SensorSample'', bytes(1:end-1), ''Delimited'', true, ''Where'', @(C) C.status > 0)';
asserts.checkerror(cmd, ...
  'The data ends inside a record (27 bytes left).', 'protobuf:decodeError:dataEndsInsideRecordBytes');
%=============================================================================
