%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% Reference corpus (reference/, not distributed): every supported .proto file gives the
% descriptor of protoc 36.2, every other file is refused; descriptor sets written by protoc
% load to the same schema, and the sets we write load back to it. Files out of scope are refused
% for one of two reasons only: edition 2026 (experimental in protoc 36.2) or MessageSet.
%=============================================================================
ROOT = modulepath('protobuf');
CASES = [ROOT, '/reference/golden/parser_cases.tsv'];
skip_testsuite(~isfile(CASES), 'reference corpus not installed (reference/golden/parser_cases.tsv).');
lines = strsplit(strtrim(fileread(CASES)), newline);
problems = {};
inScope = 0;
rejected = 0;
sets = 0;
WORK = [tempdir(), createGUID()];
mkdir(WORK);
exported = [WORK, '/exported.binpb'];
% Accepted causes of a refusal.
REASONS = {'protobuf:parseError:editionSupportedEditions', 'protobuf:parseError:messageSetNotSupported'};
causes = zeros(1, numel(REASONS));
for k = 1:numel(lines)
  parts = strsplit(strtrim(lines{k}), sprintf('\t'));
  expected = strcmp(parts{1}, '1');
  proto = [ROOT, '/', parts{2}];
  golden = [ROOT, '/', parts{3}];
  paths = {};
  if numel(parts) > 3
    paths = strcat([ROOT, '/'], strsplit(parts{4}, ';'));
  end
  % .proto file
  [sch, message, identifier] = tryLoad(proto, 'proto', paths);
  if expected
    inScope = inScope + 1;
    if isempty(sch)
      problems{end + 1} = ['rejected: ', parts{2}, ': ', message]; %#ok<SAGROW>
    elseif ~strcmp(__protobuf_dump__(sch.Id), fileread(golden))
      problems{end + 1} = ['descriptor differs: ', parts{2}]; %#ok<SAGROW>
    end
  elseif ~isempty(sch)
    problems{end + 1} = ['accepted out of scope: ', parts{2}]; %#ok<SAGROW>
  else
    rejected = rejected + 1;
    cause = strcmp(REASONS, identifier);
    causes = causes + cause;
    if ~any(cause)
      problems{end + 1} = ['refused for another reason: ', parts{2}, ': ', message]; %#ok<SAGROW>
    end
  end
  % descriptor set written by protoc
  setFile = [golden(1:end - 4), '.binpb'];
  if ~isfile(setFile)
    continue
  end
  [fromSet, message] = tryLoad(setFile, 'descriptor', {});
  if ~expected
    if ~isempty(fromSet)
      problems{end + 1} = ['descriptor set accepted out of scope: ', parts{3}]; %#ok<SAGROW>
    end
    continue
  end
  sets = sets + 1;
  if isempty(fromSet)
    problems{end + 1} = ['descriptor set rejected: ', parts{3}, ': ', message]; %#ok<SAGROW>
    continue
  end
  if ~strcmp(__protobuf_dump__(fromSet.Id), fileread(golden))
    problems{end + 1} = ['descriptor set differs: ', parts{3}]; %#ok<SAGROW>
  end
  % our descriptor set of the same schema
  fid = fopen(exported, 'w');
  fwrite(fid, protobuf.descriptor(fromSet), 'uint8');
  fclose(fid);
  again = protobuf.load(exported, 'Format', 'descriptor');
  if ~strcmp(__protobuf_dump__(again.Id), __protobuf_dump__(fromSet.Id))
    problems{end + 1} = ['exported descriptor set differs: ', parts{3}]; %#ok<SAGROW>
  end
end
rmdir(WORK, 's');
if ~isempty(problems)
  error('test:corpus', '%d problems:\n%s', numel(problems), strjoin(problems(1:min(20, end)), newline));
end
asserts.istrue(inScope >= 296);
asserts.isequal(rejected, 31);
asserts.isequal(causes, [26 5]);
asserts.isequal(sets, inScope);
%=============================================================================
function [sch, message, identifier] = tryLoad(filename, format, paths)
  sch = [];
  message = '';
  identifier = '';
  try
    sch = protobuf.load(filename, 'Format', format, 'ImportPaths', paths);
  catch e
    message = e.message;
    identifier = e.identifier;
  end
end
%=============================================================================
