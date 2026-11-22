%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% <--ADV-CLI MODE-->
% The drone fleet example runs hidden for a few frames, and its replay matches the records.
%=============================================================================
fleetFigureVisible = 'off';
fleetAnimationFrames = 5;
fleetAnimationContinuous = false;
run([modulepath('protobuf'), '/examples/drone_fleet_telemetry.m']);
asserts.isequal(fleetReport.Records, 72000);
asserts.isequal(fleetFrame, 6);
asserts.istrue(numel(fleetBytes) < fleetJsonBytes / 3);
asserts.isequal(size(fleetPX), [48 1500]);
asserts.isequal(fleetPX, double(single(fleetX)));
asserts.isequal(numel(fleetReturning.drone), sum(fleetStatus(:) == 3));
asserts.isfalse(isfile(fleetFile));
close(fleetFigure);
%=============================================================================
