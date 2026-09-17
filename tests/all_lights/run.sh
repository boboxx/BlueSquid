#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
all_lights_binary=$(mktemp /tmp/bluesquid-all-lights.XXXXXX)
trap 'rm -f "$all_lights_binary"' EXIT HUP INT TERM
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror \
  -DBLUESQUID_ALL_LIGHTS_SWITCH_PIN=35 \
  -DBLUESQUID_RGBW_RED_PIN=-1 -DBLUESQUID_RGBW_GREEN_PIN=-1 \
  -DBLUESQUID_RGBW_BLUE_PIN=-1 -DBLUESQUID_RGBW_WHITE_PIN=-1 \
  -DBLUESQUID_BED_RGBW_RED_PIN=-1 -DBLUESQUID_BED_RGBW_GREEN_PIN=-1 \
  -DBLUESQUID_BED_RGBW_BLUE_PIN=-1 -DBLUESQUID_BED_RGBW_WHITE_PIN=-1 \
  -I tests/all_lights/support -I include \
  -I lib/LightSwitchManager/src -I lib/OutputController/src \
  -I lib/EventManager/src -I lib/PwmManager/src -I lib/SettingsManager/src \
  tests/all_lights/test.cpp lib/OutputController/src/OutputController.cpp \
  lib/LightSwitchManager/src/LightSwitchManager.cpp lib/EventManager/src/EventManager.cpp \
  -o "$all_lights_binary"
"$all_lights_binary"
