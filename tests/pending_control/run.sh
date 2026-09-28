#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bluesquid-pending-test.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I include tests/pending_control/test.cpp -o "$binary"
"$binary"
echo "Pending control tests passed"
