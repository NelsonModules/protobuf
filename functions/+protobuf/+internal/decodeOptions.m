%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function options = decodeOptions(varargin)
  % Checked decode options: Delimited, Layout, OnError, MissingAsNaN, Fields, Records,
  % RowTimes, TimeUnit, Workers, WellKnownTypes, KeepUnknown, FillDefaults, EnumsAsText, Where
  % and Progress.
  defaults = struct('Delimited', false, 'Layout', 'struct', 'OnError', 'error', ...
    'MissingAsNaN', false, 'Fields', {{}}, 'Records', [], 'RowTimes', '', 'TimeUnit', 'ms', ...
    'Workers', 0, 'WellKnownTypes', true, 'KeepUnknown', false, 'FillDefaults', false, ...
    'EnumsAsText', false, 'Where', [], 'Progress', []);
  options = protobuf.internal.nameValueOptions(defaults, varargin{:});
  options.Delimited = logical(options.Delimited);
  options.MissingAsNaN = logical(options.MissingAsNaN);
  options.WellKnownTypes = logical(options.WellKnownTypes);
  options.KeepUnknown = logical(options.KeepUnknown);
  options.FillDefaults = logical(options.FillDefaults);
  options.EnumsAsText = logical(options.EnumsAsText);
  options.Layout = choice(options.Layout, {'struct', 'columns', 'table', 'timetable'}, 'Layout');
  options.OnError = choice(options.OnError, {'error', 'skip'}, 'OnError');
  options.TimeUnit = choice(options.TimeUnit, {'s', 'ms', 'us', 'ns'}, 'TimeUnit');
  options.Fields = fieldList(options.Fields);
  options.RowTimes = char(string(options.RowTimes));
  if strcmp(options.Layout, 'timetable') && isempty(options.RowTimes)
    error('protobuf:invalidOption:timetableLayoutNeedsRowTimesName', ...
      'The timetable layout needs RowTimes, the name of a time field.');
  end
  if ~isempty(options.RowTimes) && ~isempty(options.Fields) && ~any(strcmp(options.Fields, options.RowTimes))
    options.Fields{end + 1} = options.RowTimes;
  end
  options.Records = recordRange(options.Records, options.Delimited);
  workers = options.Workers;
  if ~(isnumeric(workers) && isscalar(workers) && isreal(workers) && workers >= 0 && workers == fix(workers))
    error('protobuf:invalidOption:workersNotInteger', ...
      'Workers must be a nonnegative integer (0 chooses automatically).');
  end
  options.Workers = double(workers);
  if ~isempty(options.Where)
    if ~isa(options.Where, 'function_handle')
      error('protobuf:invalidOption:whereFunctionHandleTakingColumns', ...
        'Where must be a function handle taking the columns of a block of records.');
    end
    if ~options.Delimited
      error('protobuf:invalidOption:whereNeedsDelimitedTrue', 'Where needs ''Delimited'', true.');
    end
    if ~isempty(options.Records)
      error('protobuf:invalidOption:whereRecordsUsedTogether', 'Where and Records cannot be used together.');
    end
  end
  if ~isempty(options.Progress)
    protobuf.internal.checkProgress(options.Progress);
    if ~options.Delimited
      error('protobuf:invalidOption:progressNeedsDelimitedTrue', 'Progress needs ''Delimited'', true.');
    end
    if ~isempty(options.Records)
      error('protobuf:invalidOption:progressRecordsUsedTogether', 'Progress and Records cannot be used together.');
    end
  end
end
%=============================================================================
function value = choice(value, allowed, name)
  value = lower(char(string(value)));
  if ~any(strcmp(value, allowed))
    error('protobuf:invalidOption:mustBeOneOf', ...
      '%s must be one of: %s.', name, strjoin(strcat('''', allowed, ''''), ', '));
  end
end
%=============================================================================
function names = fieldList(names)
  if isempty(names)
    names = {};
    return
  end
  if ~(ischar(names) || isstring(names) || iscellstr(names))
    error('protobuf:invalidOption:fieldsTextFieldNameList', ...
      'Fields must be text: a field name or a list of field names.');
  end
  names = reshape(cellstr(names), 1, []);
end
%=============================================================================
function range = recordRange(range, delimited)
  if isempty(range)
    range = [];
    return
  end
  valid = isnumeric(range) && isreal(range) && numel(range) == 2 && range(1) >= 1 ...
    && range(1) == fix(range(1)) && range(2) >= range(1) - 1 ...
    && (isinf(range(2)) || range(2) == fix(range(2)));
  if ~valid
    error('protobuf:invalidOption:recordsFirstLastPositiveRecord', ...
      'Records must be [first last], positive record numbers (last can be Inf).');
  end
  if ~delimited
    error('protobuf:invalidOption:recordsNeedsDelimitedTrue', 'Records needs ''Delimited'', true.');
  end
  range = double(reshape(range, 1, 2));
end
%=============================================================================
