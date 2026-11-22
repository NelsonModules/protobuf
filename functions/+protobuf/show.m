%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function show(schema, type, value, varargin)
  % Display messages in text format, one block per record: a struct (each element is a
  % record) or encoded bytes ('Delimited', true for a stream). 'MaxRecords' (10 by default)
  % limits the records shown (protobuf.totext gives the text of one message).
  narginchk(3, Inf);
  protobuf.internal.checkSchema(schema);
  defaults = struct('Delimited', false, 'MaxRecords', 10);
  options = protobuf.internal.nameValueOptions(defaults, varargin{:});
  limit = options.MaxRecords;
  if ~(isnumeric(limit) && isscalar(limit) && limit >= 0 && (isinf(limit) || limit == fix(limit)))
    error('protobuf:invalidOption:maxrecordsNonnegativeIntegerInf', 'MaxRecords must be a nonnegative integer or Inf.');
  end
  if isa(value, 'uint8')
    value = protobuf.decode(schema, type, value, 'Delimited', logical(options.Delimited));
  elseif ~isstruct(value)
    error('protobuf:invalidValue:protobufShowExpectsStructUint8', 'protobuf.show expects a struct or uint8 bytes.');
  end
  count = numel(value);
  shown = min(count, limit);
  out = '';
  for k = 1:shown
    body = protobuf.totext(schema, type, value(k));
    if isempty(body)
      body = [_('(empty message)'), newline];
    end
    if count > 1
      out = [out, sprintf(_('# record %d'), k), newline, body]; %#ok<AGROW>
    else
      out = [out, body]; %#ok<AGROW>
    end
  end
  if shown < count
    out = [out, sprintf(_('# ... %d more records'), count - shown), newline];
  end
  if count == 0
    out = [_('(no record)'), newline];
  end
  fprintf('%s', out);
end
%=============================================================================
