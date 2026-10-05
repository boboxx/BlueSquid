#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
binary=$(mktemp /tmp/bs-ota-network.XXXXXX)
trap 'rm -f "$binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I tests/ota_network/support -I include -I lib/CerboWifiManager/src tests/ota_network/test.cpp lib/CerboWifiManager/src/CerboWifiManager.cpp -o "$binary"
"$binary"
echo 'OTA network switching and Cerbo restoration tests passed'
