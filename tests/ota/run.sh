#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
work=$(mktemp -d /tmp/bs-ota.XXXXXX)
trap 'rm -rf "$work"' EXIT HUP INT TERM
python3 tests/ota/test_package.py "$work/fixture.bsfw"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I include tests/ota/test.cpp -o "$work/test"
"$work/test" "$work/fixture.bsfw"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I include tests/ota/test_credentials.cpp -o "$work/credentials"
"$work/credentials"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I include tests/ota/test_link.cpp -o "$work/link"
"$work/link"
echo 'OTA package, credential and network session tests passed'
