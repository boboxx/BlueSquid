#pragma once
#include <Arduino.h>
class SettingsManager {
 public:
  String ssid = "Cerbo", password = "cerbopassword";
  unsigned saves = 0;
  String loadCerboStationSsid(const char*) { return ssid; }
  String loadCerboStationPassword(const char*) { return password; }
  bool saveCerboStation(const String& name, const String& key) { ssid = name; password = key; ++saves; return true; }
};
