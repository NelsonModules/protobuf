# Reference material

This folder holds third-party material used to test the protobuf module. It is not part of
the installed module (see `.nmignore`). The test `tests/test_protobuf_corpus.m` uses it and is
skipped when the folder is missing.

The files were fetched on 2026-11-12.

## spec

Markdown sources of the protobuf.dev documentation, copied without change from the repository
protocolbuffers/protocolbuffers.github.io (branch main). They cover the proto3 and editions
grammars, the text format, the wire format, the JSON mapping, field presence, enums, editions
features and limits. The licence is in `spec/LICENSE`. No test reads these files.

## protos

Official `.proto` files, kept in their original folder layout:

- `protobuf-v36.2`: protocolbuffers/protobuf, tag v36.2, BSD-3-Clause licence. All proto3 and
  editions files, and the proto2 files they import.
- `googleapis`: googleapis/googleapis, commit ac683aa7, Apache-2.0 licence. A subset: rpc, type,
  api, longrunning, pubsub and speech.
- `opentelemetry`: open-telemetry/opentelemetry-proto v1.11.1, Apache-2.0 licence.
- `grpc`: grpc/grpc v1.84.3, Apache-2.0 licence. The helloworld and route_guide examples.

`manifest.tsv` lists each file with its syntax or edition and its number of lines.

Files with the edition `UNSTABLE` and six files that import paths internal to Google were left
out.

## golden

`golden/descriptors` holds, for each file of `protos`, the output of protoc 36.2:

- `<file>.desc.binpb`: the FileDescriptorSet written by protoc, imports included.
- `<file>.desc.txt`: a text dump of that descriptor, made with the Python protobuf runtime.

`golden/parser_cases.tsv` lists the test cases. Each line gives a flag (1 when the module must accept
the file, 0 when it must refuse it), the `.proto` file, its dump and its import folders. 296 files
are accepted and 31 are refused. protoc itself refused 15 files; `golden/descriptors/report.json` keeps
its errors.

The 31 refused files have one of two causes, and the test checks it:

- 26 files use edition 2026. protoc 36.2 compiles them only with `--experimental_editions`, and
  the naming rules of this edition are not fully specified.
- 5 files use `message_set_wire_format` (MessageSet), directly or through an import. This old
  wire format is deprecated and only used inside Google.

## Regenerating the files

The tools are in `tools/`:

- `gen_descriptors.py` writes the descriptor sets with protoc.
- `protobuf_goldens.py descriptors <module folder>` writes the dumps and `parser_cases.tsv`.

protoc is not stored in the repository. Download the official release 36.2 for your platform
into `tools/protoc-36.2-win64` (or the matching folder name).
