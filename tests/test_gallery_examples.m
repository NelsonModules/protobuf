%=============================================================================
% Copyright (c) 2016-present Allan CORNET (Nelson)
%=============================================================================
% This file is part of the protobuf module for Nelson.
% SPDX-License-Identifier: LGPL-3.0-or-later
%=============================================================================
% examples/index.json follows the gallery rules.
%=============================================================================
helperPath = [modulepath('tests_manager', 'tests'), '/helpers'];
previousPath = addpath(helperPath);
cleanupPath = onCleanup(@() path(previousPath));
validateExampleModule('protobuf');
clear cleanupPath;
%=============================================================================
