#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bs-json.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I include -I .pio/libdeps/touchscreen_controller/ArduinoJson/src tests/configuration_json/test.cpp -o "$binary"
"$binary"
echo 'Configuration JSON tests passed'
