#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bluesquid-power-history-test.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I tests/power_history/support \
  -I src/touchscreen tests/power_history/test.cpp \
  src/touchscreen/TouchHistory.cpp -o "$binary"
"$binary"
echo "Power history tests passed"
