%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% A fleet of drones streams its telemetry as protobuf records: the stream is encoded,
% written, read back natively and replayed live in 3D.
if ~exist('fleetFigureVisible', 'var')
  fleetFigureVisible = 'on';
end
if ~exist('fleetAnimationEnabled', 'var')
  fleetAnimationEnabled = true;
end
if ~exist('fleetAnimationFrames', 'var')
  fleetAnimationFrames = 1500;
end
if ~exist('fleetAnimationContinuous', 'var')
  fleetAnimationContinuous = strcmp(fleetFigureVisible, 'on');
end
if ~exist('fleetFramePause', 'var')
  fleetFramePause = 0;
end

% Schema written as .proto text: enum, nested message and a Timestamp.
fleetSchema = protobuf.load([ ...
  'syntax = "proto3"; package demo.fleet; import "google/protobuf/timestamp.proto"; ', ...
  'enum Status { STATUS_UNSPECIFIED = 0; STATUS_FLYING = 1; STATUS_LOW_BATTERY = 2; ', ...
  'STATUS_RETURNING = 3; } ', ...
  'message Vec3 { float x = 1; float y = 2; float z = 3; } ', ...
  'message Telemetry { uint32 drone = 1; google.protobuf.Timestamp time = 2; ', ...
  'Vec3 position = 3; float speed = 4; float battery = 5; Status status = 6; }'], ...
  'Format', 'text', 'Name', 'fleet.proto');

% Simulated flight: 48 drones, 1500 samples each (one every 2 s).
fleetDrones = 48;
fleetSteps = 1500;
fleetRandom = rand(fleetDrones, 6);
fleetTime = (0:fleetSteps - 1) / 30;
fleetRadius = 18 + 26 * fleetRandom(:, 1);
fleetRate = 0.15 + 0.25 * fleetRandom(:, 2);
fleetPhase = 2 * pi * fleetRandom(:, 3);
fleetX = fleetRadius .* cos(fleetRate .* fleetTime + fleetPhase) + 6 * sin(0.7 * fleetRate .* fleetTime);
fleetY = fleetRadius .* sin(fleetRate .* fleetTime + fleetPhase) .* (0.6 + 0.4 * cos(0.3 * fleetTime));
fleetZ = 8 + 12 * fleetRandom(:, 4) + 4 * sin(1.3 * fleetRate .* fleetTime + fleetPhase);
fleetBattery = max(100 - fleetTime .* (0.9 + 1.2 * fleetRandom(:, 5)), 0);
fleetStatus = ones(fleetDrones, fleetSteps);
fleetStatus(fleetBattery < 35) = 2;
fleetStatus(fleetBattery < 15) = 3;
fleetStart = datetime(2026, 11, 13, 6, 0, 0, 'TimeZone', 'UTC');

% Columns of the records (drone fastest), encoded natively as a length-delimited stream.
fleetColumns = struct();
fleetColumns.drone = uint32(repmat((1:fleetDrones)', fleetSteps, 1));
fleetColumns.time = fleetStart + seconds(2 * repmat(0:fleetSteps - 1, fleetDrones, 1));
fleetColumns.time = fleetColumns.time(:);
fleetColumns.position = struct('x', single(fleetX(:)), 'y', single(fleetY(:)), 'z', single(fleetZ(:)));
fleetColumns.speed = single(repmat(abs(fleetRadius .* fleetRate), fleetSteps, 1));
fleetColumns.battery = single(fleetBattery(:));
fleetColumns.status = int32(fleetStatus(:));
tic();
fleetBytes = protobuf.encode(fleetSchema, 'Telemetry', fleetColumns, 'Layout', 'columns', 'Delimited', true);
fleetEncodeTime = toc();
fleetFile = [tempdir(), 'drone_fleet_telemetry.binpb'];
fleetFid = fopen(fleetFile, 'w');
fwrite(fleetFid, fleetBytes, 'uint8');
fclose(fleetFid);

% Read back: columns, enum values as names, Timestamp as datetime.
tic();
[fleetRecords, ~, fleetReport] = protobuf.read(fleetSchema, 'Telemetry', fleetFile, ...
  'Delimited', true, 'Layout', 'columns', 'EnumsAsText', true);
fleetReadTime = toc();
% Only the records of returning drones, filtered block by block while reading.
tic();
fleetReturning = protobuf.read(fleetSchema, 'Telemetry', fleetFile, 'Delimited', true, ...
  'Layout', 'columns', 'Fields', {'drone', 'time', 'status'}, 'Where', @(c) c.status == 3);
fleetWhereTime = toc();
fleetJsonBytes = numel(protobuf.tojson(fleetSchema, 'Telemetry', fleetBytes, 'Delimited', true, 'NDJSON', true));
delete(fleetFile);

% Replay from the decoded columns only.
fleetPX = reshape(double(fleetRecords.position.x), fleetDrones, fleetSteps);
fleetPY = reshape(double(fleetRecords.position.y), fleetDrones, fleetSteps);
fleetPZ = reshape(double(fleetRecords.position.z), fleetDrones, fleetSteps);
fleetLevel = reshape(double(fleetRecords.battery), fleetDrones, fleetSteps);
fleetClock = reshape(fleetRecords.time, fleetDrones, fleetSteps);
fleetNames = reshape(fleetRecords.status, fleetDrones, fleetSteps);
fleetCount = [sum(strcmp(fleetNames, 'STATUS_FLYING'), 1); ...
  sum(strcmp(fleetNames, 'STATUS_LOW_BATTERY'), 1); sum(strcmp(fleetNames, 'STATUS_RETURNING'), 1)];

fleetBack = [0.03 0.04 0.08];
fleetInk = [0.86 0.90 0.97];
fleetColors = [0.30 0.85 1.00; 1.00 0.72 0.25; 1.00 0.32 0.36];
fleetFigure = figure('Name', 'Drone fleet telemetry (protobuf)', 'NumberTitle', 'off', ...
  'Color', fleetBack, 'Visible', fleetFigureVisible, 'Position', [60 60 1280 780]);

% 3D replay: trails, drones coloured by battery, returning drones ringed in red.
uicontrol('Parent', fleetFigure, 'Style', 'text', 'String', 'DRONE FLEET TELEMETRY  -  protobuf live replay', ...
  'Position', [20 742 760 30], 'HorizontalAlignment', 'left', 'BackgroundColor', fleetBack, ...
  'ForegroundColor', [0.30 0.85 1.00], 'FontSize', 16, 'FontWeight', 'bold');
fleetAxes = axes('Parent', fleetFigure, 'Position', [-0.04 0.06 0.74 0.86], 'Color', fleetBack);
% Prevent accidental pan or rotation from disturbing the live animation.
disableDefaultInteractivity(fleetAxes);
hold(fleetAxes, 'on');
fleetTrail = plot3(fleetAxes, NaN, NaN, NaN, '-', 'Color', [0.25 0.55 0.85], 'LineWidth', 1.2);
fleetHeads = scatter3(fleetAxes, fleetPX(:, 1), fleetPY(:, 1), fleetPZ(:, 1), 90, fleetLevel(:, 1), 'filled');
fleetAlert = plot3(fleetAxes, NaN, NaN, NaN, 'o', 'MarkerSize', 16, 'LineWidth', 2, ...
  'Color', fleetColors(3, :));
[fleetGX, fleetGY] = meshgrid(-55:10:55);
plot3(fleetAxes, fleetGX, fleetGY, zeros(size(fleetGX)), 'Color', [0.16 0.20 0.30]);
plot3(fleetAxes, fleetGX', fleetGY', zeros(size(fleetGX)), 'Color', [0.16 0.20 0.30]);
hold(fleetAxes, 'off');
% Battery colours: red (empty), yellow, green (full), given as RGB to each drone.
fleetMap = interp1([0; 0.5; 1], [1 0.25 0.30; 1 0.80 0.20; 0.30 1 0.55], linspace(0, 1, 256)');
fleetShade = @(level) fleetMap(1 + round(min(max(level, 0), 100) * 2.55), :);
set(fleetHeads, 'CData', fleetShade(fleetLevel(:, 1)));
colormap(fleetFigure, fleetMap);
caxis(fleetAxes, [0 100]);
fleetBar = colorbar(fleetAxes);
set(fleetBar, 'Position', [0.64 0.25 0.012 0.45], 'Color', fleetInk);
uicontrol('Parent', fleetFigure, 'Style', 'text', 'String', 'battery %', 'Position', [796 552 80 20], ...
  'HorizontalAlignment', 'left', 'BackgroundColor', fleetBack, 'ForegroundColor', fleetInk, 'FontSize', 10);
set(fleetAxes, 'XLim', [-55 55], 'YLim', [-55 55], 'ZLim', [0 30], 'DataAspectRatio', [1 1 0.45]);
axis(fleetAxes, 'off');
view(fleetAxes, -35, 28);
camva(fleetAxes, 6);
fleetTitle = uicontrol('Parent', fleetFigure, 'Style', 'text', 'String', '', 'Position', [20 712 700 26], ...
  'HorizontalAlignment', 'left', 'BackgroundColor', fleetBack, 'ForegroundColor', fleetInk, 'FontSize', 13);

% Size of the same stream: protobuf against NDJSON.
fleetSizeAxes = axes('Parent', fleetFigure, 'Position', [0.70 0.68 0.27 0.22], 'Color', fleetBack);
barh(fleetSizeAxes, [numel(fleetBytes); fleetJsonBytes] / 1e6, 'FaceColor', [0.30 0.85 1.00]);
set(fleetSizeAxes, 'YTickLabel', {'protobuf', 'NDJSON'}, 'XColor', fleetInk, 'YColor', fleetInk);
xlabel(fleetSizeAxes, 'MB', 'Color', fleetInk);
title(fleetSizeAxes, sprintf('%.1fx smaller than JSON', fleetJsonBytes / numel(fleetBytes)), ...
  'Color', fleetInk);

% Drones per status over time, drawn as the replay advances.
fleetStatusAxes = axes('Parent', fleetFigure, 'Position', [0.70 0.22 0.27 0.34], 'Color', fleetBack);
hold(fleetStatusAxes, 'on');
fleetStatusLines = gobjects(3, 1);
for fleetK = 1:3
  fleetStatusLines(fleetK) = plot(fleetStatusAxes, NaN, NaN, 'Color', fleetColors(fleetK, :), 'LineWidth', 2);
end
hold(fleetStatusAxes, 'off');
set(fleetStatusAxes, 'XLim', [0 fleetSteps * 2 / 60], 'YLim', [0 fleetDrones], 'XColor', fleetInk, ...
  'YColor', fleetInk);
xlabel(fleetStatusAxes, 'minutes', 'Color', fleetInk);
title(fleetStatusAxes, 'drones per status', 'Color', fleetInk);
fleetLabels = {'flying', 'low battery', 'returning'};
for fleetK = 1:3
  text(fleetStatusAxes, 2, fleetDrones * (1.02 - 0.09 * fleetK), fleetLabels{fleetK}, ...
    'Color', fleetColors(fleetK, :), 'FontSize', 11, 'FontWeight', 'bold');
end

fleetInfo = sprintf(['%d records - protobuf %.2f MB (NDJSON %.2f MB) - encoded in %.0f ms, read in ', ...
  '%.0f ms (%.1f M records/s) - Where kept %d returning records in %.0f ms'], fleetReport.Records, ...
  numel(fleetBytes) / 1e6, fleetJsonBytes / 1e6, 1000 * fleetEncodeTime, 1000 * fleetReadTime, ...
  fleetReport.Records / fleetReadTime / 1e6, numel(fleetReturning.drone), 1000 * fleetWhereTime);
uicontrol('Parent', fleetFigure, 'Style', 'text', 'String', fleetInfo, 'Position', [16 14 1250 24], ...
  'HorizontalAlignment', 'left', 'BackgroundColor', fleetBack, 'ForegroundColor', fleetInk, 'FontSize', 11);

fleetTrailLength = 90;
fleetStride = max(1, floor(fleetSteps / fleetAnimationFrames));
fleetFrame = 0;
fleetStep = 1;
while isgraphics(fleetFigure)
  fleetFrame = fleetFrame + 1;
  if ~fleetAnimationEnabled || (fleetFrame > fleetAnimationFrames && ~fleetAnimationContinuous)
    break
  end
  try
    fleetFirst = max(1, fleetStep - fleetTrailLength);
    fleetPad = NaN(fleetDrones, 1);
    fleetTX = [fleetPX(:, fleetFirst:fleetStep), fleetPad]';
    fleetTY = [fleetPY(:, fleetFirst:fleetStep), fleetPad]';
    fleetTZ = [fleetPZ(:, fleetFirst:fleetStep), fleetPad]';
    set(fleetTrail, 'XData', fleetTX(:), 'YData', fleetTY(:), 'ZData', fleetTZ(:));
    set(fleetHeads, 'XData', fleetPX(:, fleetStep), 'YData', fleetPY(:, fleetStep), ...
      'ZData', fleetPZ(:, fleetStep), 'CData', fleetShade(fleetLevel(:, fleetStep)));
    fleetBack3 = strcmp(fleetNames(:, fleetStep), 'STATUS_RETURNING');
    set(fleetAlert, 'XData', fleetPX(fleetBack3, fleetStep), 'YData', fleetPY(fleetBack3, fleetStep), ...
      'ZData', fleetPZ(fleetBack3, fleetStep));
    fleetMinutes = (0:fleetStep - 1) * 2 / 60;
    for fleetK = 1:3
      set(fleetStatusLines(fleetK), 'XData', fleetMinutes, 'YData', fleetCount(fleetK, 1:fleetStep));
    end
    set(fleetTitle, 'String', sprintf('%s UTC - mean battery %.0f %%', ...
      char(fleetClock(1, fleetStep), 'HH:mm:ss'), mean(fleetLevel(:, fleetStep))));
    view(fleetAxes, -35 + 0.12 * fleetFrame, 28 + 6 * sin(fleetFrame / 90));
    drawnow();
  catch
    % The figure was closed mid-frame; stop the animation cleanly.
    break
  end
  if fleetFramePause > 0
    pause(fleetFramePause);
  end
  fleetStep = fleetStep + fleetStride;
  if fleetStep > fleetSteps
    fleetStep = 1;
  end
end
%=============================================================================
