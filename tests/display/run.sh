#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bluesquid-display-test.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I tests/display/support -I include -I src/touchscreen tests/display/test.cpp src/touchscreen/TouchClock.cpp -o "$binary"
"$binary"
echo "Display schedule and clock tests passed"
