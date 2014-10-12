#!/bin/bash

set -x

REPEAT="--gtest_repeat=20"
REPEAT=""

./run_tests $REPEAT --gtest_color=yes --gtest_filter='*Compact*'

