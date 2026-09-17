#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bluesquid-rvc.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I tests/rvc/support -I include -I lib/RvcFanManager/src tests/rvc/test.cpp lib/RvcFanManager/src/RvcFanManager.cpp -o "$binary"
"$binary"
