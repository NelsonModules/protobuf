# protobuf

Protocol Buffers for [Nelson](https://nelson-lang.github.io/).

This module adds Protocol Buffers support to Nelson. It parses `.proto` schemas
at run time and encodes and decodes protobuf data through a native C++ core. It
does not depend on Google's `libprotobuf`, and it does not require a code
generation step: you load a schema and use it directly from the Nelson language.

## Features

* proto2, proto3, and editions 2023 and 2024.
* Runtime parsing of `.proto` files, with their imports. The
  `google/protobuf` well-known types are built in.
* Reading of `FileDescriptorSet` files written by `protoc`
  (`.binpb`, `.pb`, `.desc`, `.protoset`).
* Encoding and decoding of single messages and of length-delimited record
  streams, as struct arrays, columns with presence masks, tables, or
  timetables.
* Canonical proto3 JSON and the protobuf text format, in both directions,
  including NDJSON.
* Well-known types mapped to Nelson values: `Timestamp` as `datetime`,
  `Duration` as `duration`, wrappers as their value, and `Any`.
* Extensions, custom options, enums by name, and oneof fields.
* Helper functions to inspect bytes without a schema, show and diff messages,
  infer a schema from data, and index record files.
* Multi-threaded encoding and decoding, and memory-mapped reads, for large
  streams.

The schemas, values, bytes, text format, and JSON produced by this module are
checked against `protoc` 36.2 and the reference Python runtime.

## Requirements

* Nelson 2.0.0 or later.
* A C++ build toolchain, only if you build the native gateway yourself. A
  prebuilt library is shipped in `builtin/`.

## Usage

```matlab
% Load a schema from a .proto file.
schema = protobuf.load('addressbook.proto');

% Encode a struct to bytes.
person = struct('name', 'Ada', 'id', int32(42));
bytes = protobuf.encode(schema, 'Person', person);

% Decode the bytes back to a struct.
person = protobuf.decode(schema, 'Person', bytes);

% Convert to and from JSON and the text format.
json = protobuf.tojson(schema, 'Person', person);
text = protobuf.totext(schema, 'Person', person);
```

A schema can also be given as `.proto` text instead of a file:

```matlab
schema = protobuf.load('message Point { double x = 1; double y = 2; }', ...
  'Format', 'text', 'Name', 'point.proto');
```

See `examples/drone_fleet_telemetry.m` for a larger example that writes,
reads back, and replays a stream of telemetry records.

## Main functions

| Function | Purpose |
| --- | --- |
| `protobuf.load` | Parse a `.proto` file, `.proto` text, or a descriptor set. |
| `protobuf.encode` / `protobuf.decode` | Convert between Nelson values and bytes. |
| `protobuf.write` / `protobuf.read` | Write and read record stream files. |
| `protobuf.tojson` / `protobuf.fromjson` | Proto3 JSON, in both directions. |
| `protobuf.totext` / `protobuf.fromtext` | Protobuf text format, in both directions. |
| `protobuf.descriptor` | Write the `FileDescriptorSet` of a schema. |
| `protobuf.fields` / `protobuf.enums` | List the fields and enum values of a type. |
| `protobuf.inspect` | Read bytes without a schema (like `protoc --decode_raw`). |
| `protobuf.show` / `protobuf.diff` / `protobuf.infer` | Display, compare, and infer schemas. |
| `protobuf.Schema` / `protobuf.Decoder` / `protobuf.Writer` | Classes for schemas, streaming decode, and streaming write. |

## Build

The native gateway is built by `builder.m`, which uses Nelson's own
`dynamic_link` helpers. No CMake file is maintained by hand, and no third-party
path is hard-coded. Run:

```matlab
run('builder.m');
```

The build is skipped when the library already exists. Delete
`builtin/libnlsProtobuf_builtin.<ext>` to force a rebuild.

## Tests

The tests are written in Nelson and live in `tests/`. They check the module
against a reference corpus validated with `protoc`, golden files produced by the
Python runtime, the wire format rules, and the error messages. Run them with
`test_run`.

## License

LGPL-3.0-or-later. See `LICENSE`.
