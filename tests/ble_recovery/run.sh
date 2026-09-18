#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bluesquid-ble-recovery.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I include tests/ble_recovery/test.cpp -o "$binary"
"$binary"
