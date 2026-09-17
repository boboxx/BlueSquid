#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
sp630e_test_binary=$(mktemp /tmp/bluesquid-sp630e-test.XXXXXX)
trap 'rm -f "$sp630e_test_binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -I tests/sp630e/support -I include -I src/touchscreen \
  -I lib/RgbwBleDriverManager/src \
  tests/sp630e/test.cpp lib/RgbwBleDriverManager/src/RgbwBleDriverManager.cpp \
  -o "$sp630e_test_binary"
"$sp630e_test_binary"
