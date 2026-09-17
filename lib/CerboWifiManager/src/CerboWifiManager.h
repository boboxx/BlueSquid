#pragma once

#include <Arduino.h>

#include "SettingsManager.h"

class CerboWifiManager {
 public:
  explicit CerboWifiManager(SettingsManager& settings) : settings_(settings) {}
  bool begin();
  void update();
  bool configure(const String& ssid, const String& password);
  const String& ssid() const { return ssid_; }
  String ipAddress() const;
  bool active() const;

 private:
  bool startStation();
  SettingsManager& settings_;
  String ssid_;
  String password_;
  bool wasConnected_ = false;
  uint32_t lastAttemptMs_ = 0;
  bool restartPending_ = false;
};
