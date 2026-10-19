#pragma once

// Arduino 3.x's generated sdkconfig defines three host slots unconditionally.
// Include it first, then configure the separately compiled NimBLE-Arduino host.
// This header is force-included only in the Controller build.
#include "sdkconfig.h"
// NimBLE indexes per-connection tables by the radio's connection handle
// without a bounds check, and the radio assigns handles up to one less than
// its activity limit (raised to 10 in BleRadioCapacity.cpp). Six slots let
// handle 7 overwrite NimBLE's update list and crash the Controller once a
// second touchscreen joined the light modules.
#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 9
#undef CONFIG_NIMBLE_MAX_CONNECTIONS
#define CONFIG_NIMBLE_MAX_CONNECTIONS 9
#undef CONFIG_BT_NIMBLE_MAX_BONDS
#define CONFIG_BT_NIMBLE_MAX_BONDS 6
// NimBLE stores each bonded client's notification subscriptions. When that
// store is full it disconnects another bonded device and deletes its bond, so
// two touchscreens (five subscriptions each) evicted each other with the
// default of 8. Allow eight per bond.
#undef CONFIG_BT_NIMBLE_MAX_CCCDS
#define CONFIG_BT_NIMBLE_MAX_CCCDS 48
