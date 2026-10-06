#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bluesquid-level-test.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I include tests/level_filter/test.cpp -o "$binary"
"$binary"
echo "Level filtering tests passed"
