%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
function T = fields(schema, type)
  % Table of the fields of a message type: name, number, type, labels, oneof, JSON name,
  % required flag, default value and the other options written in the .proto text (custom
  % options such as [(units) = "mbar"] included): a struct array with Name and Value (text)
  % and Decoded (the typed value when google/protobuf/descriptor.proto and the option are
  % loaded, [] otherwise).
  % Extensions of the message are listed too, with their full name in Extension ("" for the
  % fields of the message).
  narginchk(2, 2);
  protobuf.internal.checkSchema(schema);
  s = __protobuf_fields__(schema.Id, char(type));
  T = table(s.Name, s.Number, s.Type, s.TypeName, s.Repeated, s.Map, s.Presence, s.Oneof, ...
    s.JsonName, s.Required, s.Default, s.Options, s.Extension, 'VariableNames', {'Name', ...
    'Number', 'Type', 'TypeName', 'Repeated', 'Map', 'Presence', 'Oneof', 'JsonName', 'Required', ...
    'Default', 'Options', 'Extension'});
end
%=============================================================================
