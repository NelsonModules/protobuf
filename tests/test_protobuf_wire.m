%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Wire rules of the reference runtime, truncated and corrupted input, unknown fields,
% several threads, Any, text format syntax.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
GOLDEN = [DATA, '/golden'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
%=============================================================================
% Oneof: the last member on the wire wins.
e = protobuf.decode(sch, 'Event', uint8([10 2 8 5 18 2 8 7]));
asserts.isequal(e.kind, 'stopped');
asserts.isequal(e.stopped.timestamp_ms, int64(7));
asserts.isequal(e.started, struct('timestamp_ms', {}, 'mode', {}));
% A singular sub-message given twice merges; unknown and mistyped fields are skipped.
s = protobuf.decode(sch, 'SensorSample', uint8([42 2 8 1 42 5 21 0 0 128 63 152 6 1 13 1 2 3 4]));
asserts.isequal(s.diagnostics.calibrated, true);
asserts.isequal(s.diagnostics.temperature, single(1));
asserts.isequal(s.timestamp_ms, int64(0));
asserts.isequal(s.status, []);
% Packed and unpacked repeated values are both read.
a = protobuf.decode(sch, 'AllTypes', uint8([216 2 5 218 2 2 1 2]));
asserts.isequal(a.r_int32, int32([5 1 2]));
% Groups of unknown fields are skipped whole; an end-group without start is an error.
s = protobuf.decode(sch, 'SensorSample', uint8([8 3 99 8 1 100 16 4]));
asserts.isequal([s.timestamp_ms, int64(s.status)], int64([3 4]));
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([8 3 100]))', ...
  'unexpected end-group tag in the input.', 'protobuf:decodeError:unexpectedEndGroupTagInput');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([99 8 1 108]))', ...
  'mismatched end-group tag in the input.', 'protobuf:decodeError:mismatchedEndGroupTagInput');
% UTF-8 is checked in string fields, not in bytes fields.
asserts.checkerror('protobuf.decode(sch, ''Measurement'', uint8([10 1 255]))', ...
  'string field "name" contains invalid UTF-8.', 'protobuf:decodeError:stringFieldContainsInvalidUTF');
a = protobuf.decode(sch, 'AllTypes', uint8([122 1 255]));
asserts.isequal(a.f_bytes, uint8(255));
% Invalid input.
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([0 1]))', ...
  'invalid field number in the input.', 'protobuf:decodeError:invalidFieldNumberInput');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([15]))', ...
  'invalid wire type in the input.', 'protobuf:decodeError:invalidWireTypeInput');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([8 128]))', ...
  'truncated varint in the input.', 'protobuf:decodeError:truncatedVarintInput');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([42 9 8]))', ...
  'truncated length-delimited field in the input.', 'protobuf:decodeError:truncatedLengthDelimitedFieldInput');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([29 1 2]))', ...
  'truncated fixed32 value in the input.', 'protobuf:decodeError:truncatedFixed32ValueInput');
asserts.checkerror('protobuf.decode(sch, ''AllTypes'', uint8([9 1 2]))', ...
  'truncated fixed64 value in the input.', 'protobuf:decodeError:truncatedFixed64ValueInput');
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([8 255 255 255 255 255 255 255 255 255 255 1]))', ...
  'malformed varint in the input.', 'protobuf:decodeError:malformedVarintInput');
nested = uint8([]);
for k = 1:110
  nested = [uint8(11), nested, uint8(12)]; %#ok<AGROW>
end
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', [uint8(8), uint8(1), nested])', ...
  'message nesting is too deep.', 'protobuf:decodeError:messageNestingTooDeep');
%=============================================================================
% A cut stream keeps exactly the complete records before the cut; what is kept writes the
% bytes of those records.
bytes = readBytes([GOLDEN, '/sensor_stream.nelson.test.sensors.AllTypes.binpb']);
bounds = recordBounds(bytes);
for cut = 1:97:numel(bytes) - 1
  [v, ~, report] = protobuf.decode(sch, 'AllTypes', bytes(1:cut), 'Delimited', true, 'OnError', 'skip');
  complete = nnz(bounds(2:end) <= cut);
  asserts.isequal(numel(v), complete);
  asserts.isequal(report.Records, complete);
  asserts.isequal(protobuf.encode(sch, 'AllTypes', v, 'Delimited', true, 'Workers', 1), ...
    bytes(1:bounds(complete + 1)));
end
% Random corruption: never a crash, and the records kept read back the same.
seed = uint64(12345);
for iteration = 1:150
  corrupted = bytes;
  for flip = 1:1 + mod(iteration, 6)
    [seed, r1] = nextRandom(seed);
    [seed, r2] = nextRandom(seed);
    corrupted(1 + mod(r1, numel(corrupted))) = uint8(mod(r2, 256));
  end
  [v, ~, report] = protobuf.decode(sch, 'AllTypes', corrupted, 'Delimited', true, 'OnError', 'skip');
  asserts.isequal(report.Records + report.Skipped >= 1, true);
  again = protobuf.encode(sch, 'AllTypes', v, 'Delimited', true);
  asserts.isequal(protobuf.encode(sch, 'AllTypes', protobuf.decode(sch, 'AllTypes', again, 'Delimited', true), ...
    'Delimited', true), again);
end
%=============================================================================
% Several threads: the rows, counts and errors of one thread.
for type = {'AllTypes', 'Event', 'SensorSample'}
  one = readBytes([GOLDEN, '/sensor_stream.nelson.test.sensors.', type{1}, '.binpb']);
  stream = repmat(one, 1, 40);
  serial = protobuf.decode(sch, type{1}, stream, 'Delimited', true, 'Workers', 1);
  for workers = [2 3 7]
    [parallel, ~, report] = protobuf.decode(sch, type{1}, stream, 'Delimited', true, 'Workers', workers);
    asserts.istrue(isequaln(parallel, serial));
    asserts.isequal(report.Records, numel(serial));
    asserts.isequal(protobuf.encode(sch, type{1}, parallel, 'Delimited', true, 'Workers', workers), stream);
  end
end
one = readBytes([GOLDEN, '/sensor_stream.nelson.test.sensors.SensorSample.binpb']);
stream = repmat(one, 1, 8);
bounds = recordBounds(stream);
header = bounds(151) + 1;
while stream(header) >= 128
  header = header + 1;
end
stream(header + 1) = 0;
for workers = [1 4]
  try
    protobuf.decode(sch, 'SensorSample', stream, 'Delimited', true, 'Workers', workers);
    asserts.istrue(false);
  catch err
    asserts.istrue(startsWith(err.message, 'record 151:'));
  end
  [v, ~, report] = protobuf.decode(sch, 'SensorSample', stream, 'Delimited', true, 'Workers', workers, ...
    'OnError', 'skip');
  asserts.isequal([numel(v), report.Records, report.Skipped], [199 199 1]);
end
%=============================================================================
% Pieces of any size give the records of the whole stream.
for type = {'AllTypes', 'Event', 'SensorSample', 'Summary'}
  stream = readBytes([GOLDEN, '/sensor_stream.nelson.test.sensors.', type{1}, '.binpb']);
  whole = protobuf.decode(sch, type{1}, stream, 'Delimited', true);
  for piece = [1 2 3 7 64 1000]
    d = protobuf.Decoder(sch, type{1});
    got = whole([]);
    round = 0;
    for first = 1:piece:numel(stream)
      d.push(stream(first:min(first + piece - 1, end)));
      round = round + 1;
      if mod(round, 5) == 0
        got = [got, d.read()]; %#ok<AGROW>
      end
    end
    [last, ~, report] = d.read();
    got = [got, last]; %#ok<AGROW>
    asserts.istrue(isequaln(got, whole));
    asserts.isequal(report.PendingBytes, 0);
  end
end
% A cut record waits; an invalid record is numbered from the start of the stream.
stream = readBytes([GOLDEN, '/sensor_stream.nelson.test.sensors.SensorSample.binpb']);
bounds = recordBounds(stream);
d = protobuf.Decoder(sch, 'SensorSample');
d.push(stream(1:bounds(4) + 1));
[v, ~, report] = d.read();
asserts.isequal([numel(v), report.PendingBytes], [3 1]);
bad = stream(bounds(4) + 1:bounds(5));
header = 1;
while bad(header) >= 128
  header = header + 1;
end
bad(header + 1) = 0;
bad = [bad, stream(bounds(5) + 1:end)];
try
  d.push(bad(2:end));
  asserts.istrue(false);
catch err
  asserts.istrue(startsWith(err.message, 'record 4:'));
end
%=============================================================================
% Unknown fields: an older schema keeps the other fields and writes them back.
older = protobuf.load(sprintf(['syntax = "proto3";\npackage nelson.test.sensors;\n', ...
  'message SensorSample {\n  message Diagnostics { optional bool calibrated = 1; }\n', ...
  '  int64 timestamp_ms = 1;\n  optional Diagnostics diagnostics = 5;\n}\n']), 'Format', 'text');
original = protobuf.decode(sch, 'SensorSample', stream, 'Delimited', true);
for workers = [1 3]
  kept = protobuf.decode(older, 'SensorSample', stream, 'Delimited', true, 'KeepUnknown', true, ...
    'Workers', workers);
  again = protobuf.encode(older, 'SensorSample', kept, 'Delimited', true);
  asserts.isequal(numel(again), numel(stream));
  asserts.istrue(isequaln(protobuf.decode(sch, 'SensorSample', again, 'Delimited', true), original));
end
dropped = protobuf.decode(older, 'SensorSample', stream, 'Delimited', true);
asserts.isfalse(isfield(dropped, 'UnknownFields'));
asserts.istrue(numel(protobuf.encode(older, 'SensorSample', dropped, 'Delimited', true)) < numel(stream));
%=============================================================================
% Any, as the reference runtime writes it in JSON, text format and bytes.
anySchema = protobuf.load([DATA, '/any_holder.proto']);
json = ['{"item": {"@type": "type.googleapis.com/nelson.test.any.Point", "x": 1, "y": -2}, ', ...
  '"items": [{"@type": "type.googleapis.com/google.protobuf.Timestamp", "value": "2023-11-14T22:13:20.005Z"}, ', ...
  '{"@type": "type.googleapis.com/nelson.test.any.Point", "x": 3}], "note": "n"}'];
text = sprintf(['item {\n  [type.googleapis.com/nelson.test.any.Point] {\n    x: 1\n    y: -2\n  }\n}\n', ...
  'items {\n  [type.googleapis.com/google.protobuf.Timestamp] {\n', ...
  '    seconds: 1700000000\n    nanos: 5000000\n  }\n}\n', ...
  'items {\n  [type.googleapis.com/nelson.test.any.Point] {\n    x: 3\n  }\n}\nnote: "n"\n']);
expected = uint8(sscanf(['0a3a0a29747970652e676f6f676c65617069732e636f6d2f', ...
  '6e656c736f6e2e746573742e616e792e506f696e74', ...
  ...
  '120d080110feffffffffffffffff01123c0a2d747970652e676f6f676c65617069732e636f6d2f676f6f676c65', ...
  '2e70726f746f6275662e54696d657374616d70120b0880e2cfaa0610c096b102122f0a29747970652e676f6f67', ...
  '6c65617069732e636f6d2f6e656c736f6e2e746573742e616e792e506f696e74120208031a016e'], '%2x')');
asserts.isequal(protobuf.encode(anySchema, 'Holder', protobuf.fromjson(anySchema, 'Holder', json)), expected);
asserts.isequal(protobuf.encode(anySchema, 'Holder', protobuf.fromtext(anySchema, 'Holder', text)), expected);
asserts.isequal(protobuf.totext(anySchema, 'Holder', expected), text);
asserts.istrue(isequaln(jsondecode(protobuf.tojson(anySchema, 'Holder', expected)), jsondecode(json)));
% An Any of a type missing from the schema keeps its bytes; text and JSON stay raw.
h = protobuf.decode(anySchema, 'Holder', uint8([10 12 10 6 97 47 98 46 67 120 18 2 8 1]));
asserts.isequal(h.item.TypeUrl, 'a/b.Cx');
asserts.isequal(h.item.Value, uint8([8 1]));
asserts.isequal(h.item.Message, []);
asserts.isequal(protobuf.totext(anySchema, 'Holder', h), ...
  sprintf('item {\n  type_url: "a/b.Cx"\n  value: "\\010\\001"\n}\n'));
asserts.checkerror(['protobuf.encode(anySchema, ''Holder'', struct(''item'', struct(''TypeUrl'', ''x/no.Such'', ', ...
  '''Message'', struct(''x'', 1))))'], ...
  'field "item": type of "x/no.Such" is not in the schema.', 'protobuf:invalidValue:fieldTypeSchema');
asserts.checkerror('protobuf.encode(anySchema, ''Holder'', struct(''item'', 3))', ...
  'field "item" expects a struct (TypeUrl and Message, or type_url and value).', ...
    'protobuf:invalidValue:fieldExpectsStructTypeUrlMessage');
%=============================================================================
% Text format syntax: comments, lists, angle brackets, escapes, separators, special numbers.
v = protobuf.fromtext(sch, 'AllTypes', sprintf(['# comment\nf_int32: -0x10; f_string: ''a\\x41\\101'' "b\\u00e9", ', ...
  'r_int32: [1, 2, 03]\nf_enum: BLUE m_string_int32 < key: "k" value: 4 > r_double: inf r_double: -nan\n']));
asserts.isequal(protobuf.totext(sch, 'AllTypes', v), sprintf(['f_int32: -16\nf_string: "aAAb', char(233), ...
  '"\nf_enum: BLUE\nr_double: inf\nr_double: nan\nr_int32: 1\nr_int32: 2\nr_int32: 3\n', ...
  'm_string_int32 {\n  key: "k"\n  value: 4\n}\n']));
asserts.checkerror('protobuf.fromtext(sch, ''AllTypes'', sprintf(''f_int32: 1\nnope: 2\n''))', ...
  'text format: line 2: unknown field "nope" for message "nelson.test.sensors.AllTypes".', ...
    'protobuf:textError:unknownFieldMessage');
asserts.checkerror('protobuf.fromtext(sch, ''AllTypes'', ''f_int32: "x"'')', ...
  'text format: line 1: field "f_int32" expects a number or a name, not a string.', ...
    'protobuf:textError:fieldExpectsNumberNameString');
asserts.checkerror('protobuf.fromtext(sch, ''AllTypes'', ''f_bool: maybe'')', ...
  'text format: line 1: field "f_bool" expects true or false.', 'protobuf:textError:fieldExpectsTrueFalse');
%=============================================================================
% JSON details: names, special values, well-known types.
wkt = protobuf.load([DATA, '/well_known.proto']);
r = protobuf.fromjson(wkt, 'Reading', ['{"at":"2026-11-12T10:30:15.123456Z","elapsed":"-90.250s","level":2.5,', ...
  '"count":0,"label":"","marks":["1970-01-01T00:00:00Z","2026-11-12T11:30:15+01:00"],', ...
  '"samples":[1,"NaN","-Infinity"]}']);
asserts.isequal(protobuf.tojson(wkt, 'Reading', r), ['{"at":"2026-11-12T10:30:15.123456Z","elapsed":"-90.250s",', ...
  '"level":2.5,"count":0,"label":"","marks":["1970-01-01T00:00:00Z","2026-11-12T10:30:15Z"],', ...
  '"samples":[1.0,"NaN","-Infinity"]}']);
a = protobuf.fromjson(sch, 'AllTypes', '{"f_int64":"-5","fUint64":7,"f_enum":"BLUE","r_enum":[1,"RED"]}');
asserts.isequal(protobuf.tojson(sch, 'AllTypes', a, 'FieldNames', 'proto'), ...
  '{"f_int64":"-5","f_uint64":"7","f_enum":"BLUE","r_enum":["RED","RED"]}');
asserts.checkerror('protobuf.fromjson(sch, ''AllTypes'', ''{"f_int32": 1.5}'')', ...
  'JSON: field "f_int32" expects an integer in range, not "1.5".', 'protobuf:jsonError:fieldExpectsIntegerRange');
%=============================================================================
% Schema-less view: groups, and input that is not a message.
asserts.isequal(protobuf.inspect(uint8([11 8 1 12])), sprintf('1 {\n  1: 1\n}\n'));
asserts.checkerror('protobuf.inspect(uint8(8))', 'not a valid protobuf message.', ...
  'protobuf:decodeError:validProtobufMessage');
% Fields: every wire type, nested messages, text only for printable UTF-8.
[txt, f] = protobuf.inspect(uint8([8 150 1 17 1 0 0 0 0 0 0 0 29 2 0 0 0 34 2 104 105 42 1 7 ...
  50 2 195 40 58 2 8 1 67 8 2 68 74 0]));
asserts.isequal({f.Wire}, {'varint', 'fixed64', 'fixed32', 'bytes', 'bytes', 'bytes', 'bytes', 'group', 'bytes'});
asserts.isequal(f(1).Value, uint64(150));
asserts.isequal(f(2).Value, uint64(1));
asserts.isequal(f(3).Value, uint32(2));
asserts.isequal(f(4).Text, 'hi');
asserts.isequal(f(5).Text, '');
asserts.isequal(f(6).Text, '');
asserts.isequal(f(7).Fields.Number, 1);
asserts.isequal(f(8).Value, []);
asserts.isequal(f(8).Fields.Value, uint64(2));
asserts.isequal(f(9).Value, uint8([]));
asserts.istrue(contains(txt, '8 {'));
[~, f] = protobuf.inspect(uint8([]));
asserts.isequal(size(f), [1 0]);
% Delimited: one entry per record; a cut or invalid record is numbered.
[txt, c] = protobuf.inspect(uint8([2 8 1 0 2 8 2]), 'Delimited', true);
asserts.isequal(numel(c), 3);
asserts.isequal(size(c{2}), [1 0]);
asserts.isequal(txt, sprintf('# record 1\n1: 1\n# record 2\n# record 3\n1: 2\n'));
asserts.checkerror('protobuf.inspect(uint8([2 8 1 5 8]), ''Delimited'', true)', ...
  'record 2: truncated length-delimited field in the input.', ...
    'protobuf:decodeError:truncatedLengthDelimitedFieldInput');
asserts.checkerror('protobuf.inspect(uint8([2 8 1 1 8]), ''Delimited'', true)', ...
  'record 2: not a valid protobuf message.', 'protobuf:decodeError:recordValidProtobufMessage');
asserts.checkerror('protobuf.inspect(int8(1))', 'The encoded data must be a uint8 vector.', ...
  'protobuf:invalidBytes:encodedDataUint8Vector');
%=============================================================================
% A oneof member replaced by another of a different kind on the wire.
a = protobuf.decode(sch, 'AllTypes', uint8([146 5 1 120 136 5 7]));
asserts.isequal(a.choice, 'c_int32');
asserts.isequal(a.c_int32, int32(7));
asserts.isequal(a.c_string, []);
a = protobuf.decode(sch, 'AllTypes', uint8([136 5 7 146 5 1 120]));
asserts.isequal(a.choice, 'c_string');
asserts.isequal(a.c_int32, []);
% A record dropped after unknown fields of earlier rows: what is kept writes back unchanged.
older = protobuf.load('syntax = "proto3"; message M { int32 a = 1; }', 'Format', 'text');
stream = uint8([4 8 1 16 2  3 8 3 0  4 8 5 16 6]);
[v, ~, report] = protobuf.decode(older, 'M', stream, 'Delimited', true, 'KeepUnknown', true, 'OnError', 'skip');
asserts.isequal([numel(v), report.Skipped], [2 1]);
asserts.isequal(protobuf.encode(older, 'M', v, 'Delimited', true), uint8([4 8 1 16 2 4 8 5 16 6]));
%=============================================================================
% Closed enums: a packed list keeps the known values; the others go to the unknown fields.
ed = protobuf.load([DATA, '/editions_2023.proto']);
r = protobuf.decode(ed, 'Reading', uint8([8 3 58 3 5 7 1]), 'KeepUnknown', true);
asserts.isequal(r.history, int32([5 1]));
asserts.isequal(r.UnknownFields, uint8([56 7]));
% A required message field must be set to write the message.
req = protobuf.load(['edition = "2023"; message In { int32 x = 1; } message M { In in = 1 ', ...
  '[features.field_presence = LEGACY_REQUIRED]; }'], ...
  'Format', 'text');
asserts.checkerror('protobuf.encode(req, ''M'', struct())', 'required field "in" of "M" is missing.', ...
  'protobuf:encodeError:requiredFieldMissing');
asserts.isequal(protobuf.encode(req, 'M', struct('in', struct('x', 1))), uint8([10 2 8 1]));
% Default bytes and strings with escapes are read as their bytes.
def = protobuf.load('edition = "2023"; message D { bytes b = 1 [default = "\n\r\t\\\001x"]; }', 'Format', 'text');
asserts.isequal(protobuf.fields(def, 'D').Default, "\n\r\t\\\001x");
d = protobuf.decode(def, 'D', protobuf.encode(def, 'D', struct('b', uint8([10 13 9 92 1 120]))));
asserts.isequal(d.b, uint8([10 13 9 92 1 120]));
%=============================================================================
% Released schemas and decoders.
s2 = protobuf.load([DATA, '/sensor_stream.proto']);
id = s2.Id;
delete(s2);
asserts.checkerror('__protobuf_fields__(id, ''SensorSample'')', 'Invalid or released protobuf schema.', ...
  'protobuf:invalidSchema:invalidReleasedProtobufSchema');
d = protobuf.Decoder(sch, 'SensorSample');
id = d.Id;
delete(d);
asserts.checkerror('__protobuf_decoder_read__(id, struct())', 'Invalid or released protobuf decoder.', ...
  'protobuf:invalidDecoder:invalidReleasedProtobufDecoder');
%=============================================================================
% An empty data file reads as no record.
WORK = [tempdir(), createGUID()];
mkdir(WORK);
fid = fopen([WORK, '/empty.pb'], 'w');
fclose(fid);
[v, ~, report] = protobuf.read(sch, 'SensorSample', [WORK, '/empty.pb'], 'Delimited', true);
asserts.isequal([numel(v), report.Records], [0 0]);
rmdir(WORK, 's');
%=============================================================================
function bytes = readBytes(filename)
  fid = fopen(filename, 'r');
  bytes = fread(fid, Inf, '*uint8')';
  fclose(fid);
end
%=============================================================================
function bounds = recordBounds(bytes)
  % 0 then the end offset of each record of a length-delimited stream.
  bounds = 0;
  p = 1;
  while p <= numel(bytes)
    len = 0;
    shift = 0;
    b = 128;
    while b >= 128
      b = double(bytes(p));
      p = p + 1;
      len = len + bitand(b, 127) * 2^shift;
      shift = shift + 7;
    end
    p = p + len;
    bounds(end + 1) = p - 1; %#ok<AGROW>
  end
end
%=============================================================================
function [seed, value] = nextRandom(seed)
  % Small linear congruential generator: the same corruptions on every run.
  seed = mod(seed * uint64(1103515245) + uint64(12345), uint64(2147483648));
  value = double(seed);
end
%=============================================================================
