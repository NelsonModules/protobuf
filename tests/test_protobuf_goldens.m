%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% Golden records written by the reference runtime (tests/data/golden, protobuf_goldens.py):
% schemas, decoded values, bytes written back, text format, JSON and protoc --decode_raw.
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
DATA = [modulepath('protobuf'), '/tests/data'];
GOLDEN = [DATA, '/golden'];
protos = dir([DATA, '/*.proto']);
check(numel(protos) >= 5, 'test .proto files');
streams = 0;
formatted = 0;
for p = 1:numel(protos)
  [~, stem] = fileparts(protos(p).name);
  sch = protobuf.load([DATA, '/', protos(p).name]);
  asserts.isequal(__protobuf_dump__(sch.Id), fileread([GOLDEN, '/', stem, '.desc.txt']));
  files = dir([GOLDEN, '/', stem, '.*.binpb']);
  for f = 1:numel(files)
    name = files(f).name;
    type = name(numel(stem) + 2:end - 6);
    base = [GOLDEN, '/', name(1:end - 6)];
    bytes = readBytes([GOLDEN, '/', name]);
    streams = streams + 1;
    % Values decoded like the reference runtime, and the same bytes written back.
    asserts.isequal(__protobuf_dump__(sch.Id, type, bytes), fileread([base, '.dump.txt']));
    [S, ~, report] = protobuf.decode(sch, type, bytes, 'Delimited', true);
    asserts.isequal(report.Skipped, 0);
    asserts.isequal(report.Records, numel(S));
    raw = protobuf.decode(sch, type, bytes, 'Delimited', true, 'WellKnownTypes', false);
    asserts.isequal(protobuf.encode(sch, type, raw, 'Delimited', true), bytes);
    % datetime and duration values write the canonical form of their instant or length, which
    % then reads and writes back unchanged.
    once = protobuf.encode(sch, type, S, 'Delimited', true);
    twice = protobuf.encode(sch, type, protobuf.decode(sch, type, once, 'Delimited', true), 'Delimited', true);
    check(isequal(once, twice), [name, ': struct values written and read back']);
    % Columns cannot tell an empty sub-message from an absent one: what they write reads and
    % writes back unchanged, with the same presence.
    [C, P] = protobuf.decode(sch, type, bytes, 'Delimited', true, 'Layout', 'columns');
    once = protobuf.encode(sch, type, C, 'Layout', 'columns', 'Presence', P, 'Delimited', true);
    [C2, P2] = protobuf.decode(sch, type, once, 'Delimited', true, 'Layout', 'columns');
    twice = protobuf.encode(sch, type, C2, 'Layout', 'columns', 'Presence', P2, 'Delimited', true);
    check(isequal(once, twice) && isequal(P2, P), [name, ': columns written and read back']);
    if ~isfile([base, '.text.txt'])
      continue
    end
    % One record at a time: text format, JSON and the schema-less view of the reference.
    texts = strsplit(fileread([base, '.text.txt']), sprintf('===\n'), 'CollapseDelimiters', false);
    jsons = strsplit(strtrim(fileread([base, '.json.txt'])), newline);
    raws = strsplit(fileread([base, '.raw.txt']), sprintf('===\n'), 'CollapseDelimiters', false);
    records = splitRecords(bytes);
    asserts.isequal(numel(records), numel(S));
    asserts.isequal(numel(texts), numel(S));
    asserts.isequal(numel(jsons), numel(S));
    for r = 1:numel(records)
      formatted = formatted + 1;
      label = sprintf('%s record %d', name, r);
      % Raw well-known messages: a duration (double seconds) cannot hold every nanosecond.
      value = raw(r);
      text = protobuf.totext(sch, type, value);
      sameText(text, texts{r}, [label, ' text']);
      sameText(protobuf.totext(sch, type, records{r}), texts{r}, [label, ' text of bytes']);
      % Text read back: the reference text and ours (maps are printed sorted by key).
      fromText = protobuf.fromtext(sch, type, texts{r}, 'WellKnownTypes', false);
      sameText(protobuf.totext(sch, type, fromText), texts{r}, ...
        [label, ' text read']);
      json = protobuf.tojson(sch, type, value);
      sameJson(json, jsons{r}, [label, ' JSON']);
      % The reference JSON and ours read as the same message, which then writes and reads back
      % unchanged (null reads as the default value, as in the reference runtime).
      once = protobuf.tojson(sch, type, protobuf.fromjson(sch, type, json, 'WellKnownTypes', false));
      sameJson(protobuf.tojson(sch, type, protobuf.fromjson(sch, type, jsons{r}, 'WellKnownTypes', false)), ...
        once, [label, ' reference JSON read']);
      sameJson(protobuf.tojson(sch, type, protobuf.fromjson(sch, type, once, 'WellKnownTypes', false)), ...
        once, [label, ' JSON read twice']);
      sameText(protobuf.inspect(records{r}), raws{r}, [label, ' raw']);
    end
  end
end
check(streams >= 17, sprintf('%d golden streams', streams));
check(formatted >= 300, sprintf('%d formatted records', formatted));
%=============================================================================
function bytes = readBytes(filename)
  fid = fopen(filename, 'r');
  bytes = fread(fid, Inf, '*uint8')';
  fclose(fid);
end
%=============================================================================
function records = splitRecords(bytes)
  % Bytes of each record of a length-delimited stream (size varint removed).
  records = {};
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
    records{end + 1} = bytes(p:p + len - 1); %#ok<AGROW>
    p = p + len;
  end
end
%=============================================================================
function sameJson(actual, expected, label)
  % Same JSON content, whatever the order of the members and the spaces.
  if ~isequaln(jsondecode(hexKeys(actual)), jsondecode(hexKeys(expected)))
    error('test:goldens', '%s\n  ours: %s\n  ref:  %s', label, actual, expected);
  end
end
%=============================================================================
function out = hexKeys(json)
  % Object keys renamed k<hex of their UTF-8 bytes>: any key becomes a field name, so two
  % JSON texts compare by content whatever the order of their members.
  out = '';
  k = 1;
  n = numel(json);
  while k <= n
    c = json(k);
    if c ~= '"'
      out(end + 1) = c; %#ok<AGROW>
      k = k + 1;
      continue
    end
    j = k + 1;
    while json(j) ~= '"'
      if json(j) == '\'
        j = j + 1;
      end
      j = j + 1;
    end
    literal = json(k:j);
    rest = strtrim(json(j + 1:end));
    if ~isempty(rest) && rest(1) == ':'
      key = jsondecode(literal);
      literal = ['"k', lower(reshape(dec2hex(unicode2native(key, 'UTF-8'))', 1, [])), '"'];
    end
    out = [out, literal]; %#ok<AGROW>
    k = j + 1;
  end
end
%=============================================================================
function check(condition, label)
  if ~condition
    error('test:goldens', 'failed: %s', label);
  end
end
%=============================================================================
function sameText(actual, expected, label)
  if strcmp(actual, expected)
    return
  end
  a = strsplit(actual, newline);
  e = strsplit(expected, newline);
  k = 1;
  while k <= min(numel(a), numel(e)) && strcmp(a{k}, e{k})
    k = k + 1;
  end
  lineA = '<end>';
  lineE = '<end>';
  if k <= numel(a)
    lineA = a{k};
  end
  if k <= numel(e)
    lineE = e{k};
  end
  error('test:goldens', '%s differs at line %d\n  ours: %s\n  ref:  %s', label, k, lineA, lineE);
end
%=============================================================================
