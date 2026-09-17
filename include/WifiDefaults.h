#pragma once

#ifndef BLUESQUID_CERBO_WIFI_SSID
#define BLUESQUID_CERBO_WIFI_SSID ""
#endif
#ifndef BLUESQUID_CERBO_WIFI_PASSWORD
#define BLUESQUID_CERBO_WIFI_PASSWORD ""
#endif
#ifndef BLUESQUID_HOTSPOT_SSID
#define BLUESQUID_HOTSPOT_SSID "BlueSquid"
#endif
#ifndef BLUESQUID_HOTSPOT_PASSWORD
#define BLUESQUID_HOTSPOT_PASSWORD "1234567890"
#endif

namespace WifiDefaults {
constexpr char cerboSsid[] = BLUESQUID_CERBO_WIFI_SSID;
constexpr char cerboPassword[] = BLUESQUID_CERBO_WIFI_PASSWORD;
constexpr char hotspotSsid[] = BLUESQUID_HOTSPOT_SSID;
constexpr char hotspotPassword[] = BLUESQUID_HOTSPOT_PASSWORD;
static_assert(sizeof(cerboSsid) <= 33, "Cerbo SSID must be at most 32 bytes");
static_assert(sizeof(cerboSsid) == 1 ||
              (sizeof(cerboPassword) >= 9 && sizeof(cerboPassword) <= 64),
              "Configured Cerbo hotspot requires an 8-63 byte password");
static_assert(sizeof(hotspotSsid) > 1 && sizeof(hotspotSsid) <= 33,
              "System hotspot SSID must be 1-32 bytes");
static_assert(sizeof(hotspotPassword) >= 9 && sizeof(hotspotPassword) <= 64,
              "System hotspot password must be 8-63 bytes");
}
