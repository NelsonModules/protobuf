%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% protobuf.load: .proto files parsed at run time.
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
sch = protobuf.load([DATA, '/sensor_stream.proto']);
asserts.istrue(isa(sch, 'protobuf.Schema'));
asserts.isequal(sch.Package, 'nelson.test.sensors');
asserts.isequal(sch.File, [DATA, '/sensor_stream.proto']);
asserts.istrue(any(strcmp(sch.Messages, 'nelson.test.sensors.SensorSample')));
asserts.istrue(any(strcmp(sch.Messages, 'nelson.test.sensors.Event.StepCompleted')));
% Map entries are internal types.
asserts.isequal(numel(sch.Messages), 10);
%=============================================================================
asserts.checkerror('protobuf.load(''missing.proto'')', 'File not found: missing.proto', ...
  'protobuf:fileNotFound:noSuchFile');
WORK = [tempdir(), createGUID()];
mkdir(WORK);
BAD = [WORK, '/bad.proto'];
fid = fopen(BAD, 'w');
fprintf(fid, 'syntax = "proto3";\nmessage A { int32 a = 1; int32 b = 1; }\n');
fclose(fid);
try
  protobuf.load(BAD);
  asserts.istrue(false);
catch e
  asserts.istrue(startsWith(e.identifier, 'protobuf:parseError:'));
  asserts.istrue(~isempty(strfind(e.message, 'bad.proto:2:')));
  asserts.istrue(~isempty(strfind(e.message, 'already used')));
end
fid = fopen(BAD, 'w');
fprintf(fid, 'syntax = "proto1";\nmessage A { optional int32 a = 1; }\n');
fclose(fid);
asserts.checkerror('protobuf.load(BAD)', [BAD, ':1:10: syntax "proto1" is not supported (proto2 and proto3 are).'], ...
  'protobuf:parseError:syntaxSupportedProto2Proto3');
rmdir(WORK, 's');
%=============================================================================
% A deleted schema can no longer be used.
delete(sch);
asserts.checkerror('protobuf.decode(sch, ''SensorSample'', uint8([]))', ...
  'Expected a schema returned by protobuf.load.', 'protobuf:invalidSchema:expectedSchemaReturnedProtobufLoad');
%=============================================================================
