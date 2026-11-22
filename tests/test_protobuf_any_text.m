%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% protobuf.inspect, protobuf.fromtext and google.protobuf.Any.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
bytes = protobuf.encode(sch, 'Event', struct('started', struct('timestamp_ms', 1, 'mode', 'auto')));
[txt, f] = protobuf.inspect(bytes);
asserts.isequal(txt, sprintf('1 {\n  1: 1\n  2: "auto"\n}\n'));
asserts.isequal(f.Number, 1);
asserts.isequal(f.Wire, 'bytes');
asserts.isequal(f.Fields(2).Text, 'auto');
asserts.isequal(f.Fields(1).Value, uint64(1));
[txt, c] = protobuf.inspect([uint8(numel(bytes)), bytes, uint8(numel(bytes)), bytes], 'Delimited', true);
asserts.isequal(numel(c), 2);
asserts.istrue(startsWith(txt, '# record 1'));
asserts.checkerror('protobuf.inspect(uint8(8))', 'not a valid protobuf message.', ...
  'protobuf:decodeError:validProtobufMessage');
%=============================================================================
% Text format read back.
v = struct('timestamp_ms', 5, 'pressure', single(1.5), 'diagnostics', struct('calibrated', true));
back = protobuf.fromtext(sch, 'SensorSample', protobuf.totext(sch, 'SensorSample', v));
asserts.isequal(protobuf.encode(sch, 'SensorSample', back), protobuf.encode(sch, 'SensorSample', v));
t = protobuf.fromtext(sch, 'AllTypes', ...
  sprintf('# comment\nf_int32: 0x10\nr_int32: [1, 2]\nf_enum: BLUE\nf_string: ''a'' "b"'));
asserts.isequal(t.f_int32, int32(16));
asserts.isequal(t.r_int32, int32([1 2]));
asserts.isequal(t.f_enum, int32(-3));
asserts.isequal(t.f_string, 'ab');
asserts.checkerror('protobuf.fromtext(sch, ''SensorSample'', sprintf(''timestamp_ms: 1\nnope: 2''))', ...
  'text format: line 2: unknown field "nope" for message "nelson.test.sensors.SensorSample".', ...
    'protobuf:textError:unknownFieldMessage');
%=============================================================================
% Any: the packed message is decoded when its type is in the schema.
a = protobuf.load([DATA, '/any_holder.proto']);
h = protobuf.fromtext(a, 'Holder', sprintf(['item { [type.googleapis.com/nelson.test.any.Point] { x: 1 y: -2 } }\n', ...
  'items { [type.googleapis.com/google.protobuf.Timestamp] { seconds: 1700000000 } }']));
asserts.isequal(h.item.TypeUrl, 'type.googleapis.com/nelson.test.any.Point');
asserts.isequal(h.item.Message.y, int32(-2));
asserts.isequal(posixtime(h.items(1).Message), 1700000000);
packed = protobuf.encode(a, 'Holder', h);
asserts.isequal(protobuf.decode(a, 'Holder', packed).item.Value, h.item.Value);
json = protobuf.tojson(a, 'Holder', struct('item', struct('TypeUrl', 'type.googleapis.com/nelson.test.any.Point', ...
  'Message', struct('x', 7))));
asserts.isequal(json, '{"item":{"@type":"type.googleapis.com/nelson.test.any.Point","x":7}}');
asserts.isequal(protobuf.fromjson(a, 'Holder', json).item.Message.x, int32(7));
raw = protobuf.decode(a, 'Holder', packed, 'WellKnownTypes', false);
asserts.isequal(raw.item.type_url, 'type.googleapis.com/nelson.test.any.Point');
%=============================================================================
