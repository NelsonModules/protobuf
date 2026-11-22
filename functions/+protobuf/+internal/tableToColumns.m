%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function columns = tableToColumns(schema, type, T, rowTimes, timeUnit)
  % Struct of columns from a table or timetable: parent_child variables go back to
  % sub-message columns; the row times of a timetable fill the rowTimes field.
  flat = struct();
  names = T.Properties.VariableNames;
  for k = 1:numel(names)
    flat.(names{k}) = T.(names{k});
  end
  if istimetable(T) && ~isempty(rowTimes)
    times = T.Properties.RowTimes;
    scale = struct('s', 1, 'ms', 1e3, 'us', 1e6, 'ns', 1e9);
    if isdatetime(times)
      if isempty(times.TimeZone)
        times.TimeZone = 'UTC';
      end
      ticks = posixtime(times) * scale.(timeUnit);
    else
      ticks = seconds(times) * scale.(timeUnit);
    end
    flat.(rowTimes) = round(ticks);
  end
  columns = nest(schema, type, flat);
end
%=============================================================================
function columns = nest(schema, type, flat)
  F = protobuf.fields(schema, type);
  columns = struct();
  names = fieldnames(flat);
  messages = F(F.Type == "message" & ~F.Repeated, :);
  groups = struct();
  for k = 1:numel(names)
    name = names{k};
    owner = arrayfun(@(p) startsWith(string(name), p + "_"), messages.Name);
    if any(F.Name == name) || ~any(owner)
      columns.(name) = flat.(name);
      continue
    end
    candidates = messages.Name(owner);
    [~, longest] = max(strlength(candidates));
    parent = char(candidates(longest));
    child = name(numel(parent) + 2:end);
    if ~isfield(groups, parent)
      groups.(parent) = struct();
    end
    groups.(parent).(child) = flat.(name);
  end
  parents = fieldnames(groups);
  for k = 1:numel(parents)
    childType = char(messages.TypeName(messages.Name == parents{k}));
    columns.(parents{k}) = nest(schema, childType, groups.(parents{k}));
  end
end
%=============================================================================
