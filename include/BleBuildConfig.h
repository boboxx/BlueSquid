#pragma once

// Arduino 3.x's generated sdkconfig defines three host slots unconditionally.
// Include it first, then configure the separately compiled NimBLE-Arduino host.
// This header is force-included only in the Controller build.
#include "sdkconfig.h"
#undef CONFIG_BT_NIMBLE_MAX_CONNECTIONS
#define CONFIG_BT_NIMBLE_MAX_CONNECTIONS 6
#undef CONFIG_NIMBLE_MAX_CONNECTIONS
#define CONFIG_NIMBLE_MAX_CONNECTIONS 6
#undef CONFIG_BT_NIMBLE_MAX_BONDS
#define CONFIG_BT_NIMBLE_MAX_BONDS 6
