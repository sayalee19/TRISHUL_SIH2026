%% digitize_h2_temphum.m
% Digitizes GMV-2021B Fig.4 (temp/humidity). Y axis is LOG scale here
% (0.1 to 10), unlike H2S's Fig.4 which was linear -- don't reuse that
% axis assumption. Reference condition is 20C/65%RH (three curves:
% 40%, 65%, 85% RH), not H2S's 55%RH/30-60-85% set.
% Run this three times, once per RH curve, changing CURVE_RH each time.

clear; clc; close all;

IMG_PATH = "C:\Users\sayal\OneDrive\Pictures\Screenshots\Screenshot 2026-09-24 184248.png";
X_CAL_VALUES = [-20, 50];     % linear X axis
Y_CAL_VALUES = [0.6, 2.0];    % LOG Y axis -- this is the key change from H2S
CURVE_RH = 85;               % <-- change to 40, then 65, then 85 across three runs
TEMP_GRIDPOINTS = [-10 0 10 20 30 40 50];  % where you'll read off the final table value

img = imread(IMG_PATH);
figure; imshow(img); hold on;
title(sprintf('Click X=%d, then X=%d gridlines', X_CAL_VALUES(1), X_CAL_VALUES(2)));
[xpix_cal, ~] = ginput(2);
px2x = polyfit(xpix_cal, X_CAL_VALUES, 1);   % LINEAR, not log, for X

title(sprintf('Click Y=%.1f, then Y=%.0f gridlines', Y_CAL_VALUES(1), Y_CAL_VALUES(2)));
[~, ypix_cal] = ginput(2);
logy_cal = log10(Y_CAL_VALUES);
py2logy = polyfit(ypix_cal, logy_cal, 1);    % LOG for Y

title(sprintf('Click the %d%%RH curve markers, left to right, Enter when done', CURVE_RH));
[xpix_pts, ypix_pts] = ginput;

temp_digitized = polyval(px2x, xpix_pts);          % linear
rsrso_digitized = 10 .^ polyval(py2logy, ypix_pts); % log

[temp_digitized, sortIdx] = sort(temp_digitized);
rsrso_digitized = rsrso_digitized(sortIdx);

% Interpolate onto the fixed gridpoints your firmware table uses
rsrso_at_grid = interp1(temp_digitized, rsrso_digitized, TEMP_GRIDPOINTS, 'linear', 'extrap');

fprintf('\n%% RH curve, values at [10 20 30 40 50]C:\n');
fprintf('{ %.2f, %.2f, %.2f, %.2f, %.2f },  // %d%% RH\n', rsrso_at_grid, CURVE_RH);