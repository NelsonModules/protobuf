%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function native = nativeOptions(options)
  % Struct of decoding options read by the builtins (tables are built from columns).
  layout = options.Layout;
  if any(strcmp(layout, {'table', 'timetable'}))
    layout = 'columns';
  end
  native = struct('Delimited', options.Delimited, 'Layout', layout, ...
    'Skip', strcmp(options.OnError, 'skip'), 'MissingAsNaN', options.MissingAsNaN, ...
    'WellKnownTypes', options.WellKnownTypes, 'Fields', {options.Fields}, ...
    'Records', options.Records, 'Workers', options.Workers, 'KeepUnknown', options.KeepUnknown, ...
    'FillDefaults', options.FillDefaults, 'EnumsAsText', options.EnumsAsText);
end
%=============================================================================
