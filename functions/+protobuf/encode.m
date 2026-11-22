%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function bytes = encode(schema, type, value, varargin)
  % Encode records as protobuf bytes: a struct (one record per element with 'Delimited'),
  % a struct of columns ('Layout', 'columns'), a table or a timetable. 'Workers' sets the
  % threads encoding delimited records (0, the default, chooses from the record count).
  narginchk(3, Inf);
  protobuf.internal.checkSchema(schema);
  defaults = struct('Delimited', false, 'Layout', 'struct', 'Presence', [], 'RowTimes', '', ...
    'TimeUnit', 'ms', 'Workers', 0);
  options = protobuf.internal.nameValueOptions(defaults, varargin{:});
  workers = options.Workers;
  if ~(isnumeric(workers) && isscalar(workers) && isreal(workers) && workers >= 0 && workers == fix(workers))
    error('protobuf:invalidOption:workersNotInteger', ...
      'Workers must be a nonnegative integer (0 chooses automatically).');
  end
  layout = lower(char(string(options.Layout)));
  if istable(value) || istimetable(value)
    value = protobuf.internal.tableToColumns(schema, type, value, char(string(options.RowTimes)), ...
      lower(char(string(options.TimeUnit))));
    layout = 'columns';
  end
  if strcmp(layout, 'columns')
    if ~isstruct(value) || ~isscalar(value)
      error('protobuf:invalidValue:columnsLayoutNeedsColumns', ...
        'The columns layout needs a scalar struct of Nx1 columns.');
    end
    presence = options.Presence;
    if ~isstruct(presence)
      presence = struct();
    end
    bytes = __protobuf_encode__(schema.Id, char(type), value, logical(options.Delimited), true, ...
      presence, double(workers));
    return
  elseif ~strcmp(layout, 'struct')
    error('protobuf:invalidOption:layoutStructColumns', 'Layout must be one of: ''struct'', ''columns''.');
  end
  bytes = __protobuf_encode__(schema.Id, char(type), value, logical(options.Delimited), false, ...
    struct(), double(workers));
end
%=============================================================================
