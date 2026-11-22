%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function value = finishLayout(value, options)
  % Turn decoded columns into a table or a timetable when the layout asks for it.
  if ~any(strcmp(options.Layout, {'table', 'timetable'}))
    return
  end
  [names, columns] = flatten(value, '');
  if strcmp(options.Layout, 'table')
    value = table(columns{:}, 'VariableNames', names);
    return
  end
  key = strrep(options.RowTimes, '.', '_');
  index = find(strcmp(names, key), 1);
  if isempty(index)
    error('protobuf:invalidOption:rowtimesFieldFound', 'RowTimes field not found: %s', options.RowTimes);
  end
  times = rowTimes(columns{index}, options.TimeUnit);
  names(index) = [];
  columns(index) = [];
  value = table2timetable(table(columns{:}, 'VariableNames', names), 'RowTimes', times);
end
%=============================================================================
function [names, columns] = flatten(s, prefix)
  % One variable per column; sub-message columns become parent_child variables.
  names = {};
  columns = {};
  fields = fieldnames(s);
  for k = 1:numel(fields)
    column = s.(fields{k});
    name = [prefix, fields{k}];
    if isnumeric(column) && isequal(size(column), [0 0])
      % Recursive sub-message with no data: no variable.
      continue
    elseif isstruct(column) && isscalar(column)
      [subNames, subColumns] = flatten(column, [name, '_']);
      names = [names, subNames]; %#ok<AGROW>
      columns = [columns, subColumns]; %#ok<AGROW>
    elseif iscell(column) && (isempty(column) || iscellstr(column))
      names{end + 1} = name; %#ok<AGROW>
      columns{end + 1} = string(column); %#ok<AGROW>
    else
      names{end + 1} = name; %#ok<AGROW>
      columns{end + 1} = column; %#ok<AGROW>
    end
  end
end
%=============================================================================
function times = rowTimes(column, unit)
  % Row times: datetime and duration values as they are, numbers since 1970-01-01 UTC.
  if isdatetime(column) || isduration(column)
    times = column;
    return
  end
  if ~isnumeric(column)
    error('protobuf:invalidOption:rowtimesNameNumericDatetimeDuration', ...
      'RowTimes must name a numeric, datetime or duration field.');
  end
  scale = struct('s', 1, 'ms', 1e3, 'us', 1e6, 'ns', 1e9);
  times = datetime(double(column) / scale.(unit), 'ConvertFrom', 'posixtime', 'TimeZone', 'UTC');
end
%=============================================================================
