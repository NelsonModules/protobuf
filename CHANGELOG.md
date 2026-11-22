# Changelog

All notable changes to the `protobuf` module are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - Unreleased

First release. 

### Schemas

- `protobuf.load` parses `.proto` files at run time, with their imports, with no
  code generation and no compiler. `'ImportPaths'` lists the folders searched for
  imports, and the `google/protobuf` well-known types are built in.
- proto2, proto3, and editions 2023 and 2024 are supported. This includes
  required fields, groups, default values, closed enums, and `[packed = true]`
  for proto2, and the edition features: field presence, legacy required, closed
  enums, expanded or packed repeated fields, UTF-8 checks, delimited messages,
  default values, naming style, and `export` and `local` visibility in 2024.
- A schema can be given as `.proto` text instead of a file
  (`protobuf.load(text, 'Format', 'text')`, with `'Name'` and `'ImportPaths'`).
- `FileDescriptorSet` files written by `protoc` are also accepted
  (`.binpb`, `.pb`, `.desc`, `.protoset`). `protobuf.descriptor` writes the
  `FileDescriptorSet` of a schema.
- Extensions are supported: `extensions` ranges and `extend` blocks for proto2,
  editions, and proto3 custom option definitions. An extension is a member of the
  extended message, named `[full.name]` in the text format and in JSON.
  `google/protobuf/descriptor.proto` is built in.
- The aggregate form `features = { ... }` and
  `deprecated_legacy_json_field_conflicts` are handled.
- Parse errors give the file, line, and column.

### Encoding and decoding

- `protobuf.decode` and `protobuf.read` decode single messages or
  length-delimited record streams. The result can be a struct array, columns of
  size Nx1 with presence masks, a table, or a timetable (`'RowTimes'`).
  `'Fields'` and `'Records'` decode part of a stream, `'OnError', 'skip'`
  skips bad records, and `'MissingAsNaN'` and `'FillDefaults'` control how
  absent fields are read.
- `protobuf.encode` and `protobuf.write` encode structs, columns
  (`'Presence'`), tables, and timetables to bytes. Enum values can be given by
  name, and a oneof member can be selected. `'Append'`, open file identifiers,
  and ZIP entries (`'Entry'`) are supported for both write and read.
- `protobuf.Decoder` decodes a length-delimited stream from pieces of any size
  (`push`, then `read` the new records).
- The column layout exposes `Present`, which tells a sub-message that is present
  with no field set from one that is absent. Columns and presence encode back to
  the same bytes.
- Large streams are encoded and decoded on several threads (`'Workers'`), and
  numeric columns are copied without intermediate arrays. `protobuf.read` maps
  data files in memory instead of reading them. On a 15-field record stream this
  reaches about 4 million records per second per thread.

### Well-known types

- `Timestamp` is read as a UTC `datetime`, `Duration` as a `duration`, and the
  wrapper types as their value. `'WellKnownTypes', false` keeps them as messages.
- `google.protobuf.Any` is a struct with `TypeUrl`, `Value`, and the decoded
  `Message`, with the JSON `"@type"` and the text format `[type URL]` forms.

### Text format and JSON

- `protobuf.totext` and `protobuf.fromtext` write and read the protobuf text
  format. `protobuf.tojson` and `protobuf.fromjson` write and read canonical
  proto3 JSON. Both are checked against the reference runtime.
- NDJSON is supported: `protobuf.tojson(..., 'NDJSON', true)` writes one JSON
  object per line, and `protobuf.fromjson(..., 'NDJSON', true)` reads them.
- `'KeepUnknown'` keeps fields that are missing from the schema
  (`UnknownFields`) and writes them back.

### Inspection and tools

- `protobuf.inspect` reads bytes without a schema, like `protoc --decode_raw`.
- `protobuf.show`, `protobuf.diff`, and `protobuf.infer` display a message,
  compare two messages, and infer a `.proto` file from a struct or a table.
- `protobuf.fields` lists the fields of a message type, with `Required`,
  `Default`, `Options` (including custom options, with the decoded value and its
  type), and `Extension`.
- `protobuf.enums` lists the names and numbers of an enum type. `'EnumsAsText'`
  decodes enum values as their names.
- `protobuf.Writer` pushes records to a file, a file identifier, or a ZIP entry
  kept open. `protobuf.index` builds a persistent record index (`<file>.pbidx`)
  used by `'Records'`.
- `'Fields'` can select fields of sub-messages (`'diagnostics.drift'`), and
  `'Where'` filters a delimited stream block by block.
- A `'Progress'` callback is called during delimited reads and decodes, during
  `protobuf.write`, while `protobuf.index` scans a file, and on each
  `protobuf.Writer` push.

### Implementation

- The core is native C++ with no third-party dependency. The descriptors are
  identical to `protoc` 36.2, and the values and bytes are identical to the
  reference Python runtime.
- `examples/drone_fleet_telemetry.m` streams the telemetry of 48 drones as
  72000 records, encodes them, reads them back, compares them with NDJSON, and
  replays them live in 3D.
