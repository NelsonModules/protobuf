%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
classdef (Sealed) Decoder < handle & nelson.mixin.CustomDisplay
  % Decodes a length-delimited stream given in pieces: push bytes, then read the records.
  properties (SetAccess = private)
    Type = ''
    Layout = 'struct'
    OnError = 'error'
    MissingAsNaN = false
    Fields = {}
    RowTimes = ''
    TimeUnit = 'ms'
    Where = []
  end
  properties (SetAccess = private, Hidden)
    Id = 0
    Native = struct()
  end
  methods
    function obj = Decoder(schema, type, varargin)
      narginchk(2, Inf);
      protobuf.internal.checkSchema(schema);
      if numel(varargin) == 1 && isstruct(varargin{1})
        % Options already checked by protobuf.internal.decodeOptions.
        options = varargin{1};
      else
        options = protobuf.internal.decodeOptions('Delimited', true, varargin{:});
      end
      if ~options.Delimited
        error('protobuf:invalidOption:decoderReadsLengthDelimitedRecords', ...
          'A decoder reads length-delimited records only.');
      end
      if ~isempty(options.Records)
        error('protobuf:invalidOption:decoderDoesTakeRecords', 'A decoder does not take Records.');
      end
      obj.Native = protobuf.internal.nativeOptions(options);
      obj.Id = __protobuf_decoder_create__(schema.Id, char(type), obj.Native);
      obj.Fields = options.Fields;
      obj.RowTimes = options.RowTimes;
      obj.TimeUnit = options.TimeUnit;
      obj.Type = char(type);
      obj.Layout = options.Layout;
      obj.OnError = options.OnError;
      obj.MissingAsNaN = options.MissingAsNaN;
      obj.Where = options.Where;
    end

    function [records, skipped] = push(obj, bytes)
      % Decodes the complete records; the bytes of a cut record wait for the next push. With
      % Where, only the records it accepts are kept.
      [records, skipped] = __protobuf_decoder_push__(obj.Id, bytes);
      obj.filterRows();
    end

    function [value, presence, report] = read(obj)
      % Records decoded (and kept by Where) since the previous read, which are then forgotten.
      [value, presence, report] = __protobuf_decoder_read__(obj.Id, obj.Native);
      value = protobuf.internal.finishLayout(value, struct('Layout', obj.Layout, ...
        'RowTimes', obj.RowTimes, 'TimeUnit', obj.TimeUnit));
    end

    function delete(obj)
      if obj.Id ~= 0
        __protobuf_decoder_release__(obj.Id);
        obj.Id = 0;
      end
    end
  end
  methods (Access = private)
    function filterRows(obj)
      % Where sees the columns of the new records and returns one logical value per record.
      if isempty(obj.Where)
        return
      end
      [columns, rows] = __protobuf_decoder_hold__(obj.Id, obj.Native);
      if rows == 0
        __protobuf_decoder_keep__(obj.Id, false(0, 1));
        return
      end
      condition = obj.Where;
      __protobuf_decoder_keep__(obj.Id, condition(columns));
    end
  end
end
%=============================================================================
