#pragma once

#include <Arduino.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "OtaLink.h"

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
  bool requestUpdateNetwork(bool start, uint32_t id);
  OtaLink::Status updateNetworkStatus() const;

 private:
  bool startStation();
  void publishUpdateNetwork();
  void restoreStation();
  struct UpdateRequest { bool start = false; uint32_t id = 0; };
  QueueHandle_t updateRequests_ = nullptr;
  OtaLink::Session updateSession_;
  OtaLink::Status updateStatus_;
  String updateSsid_;
  std::atomic<bool> updateNetworkActive_{false};
  mutable portMUX_TYPE updateMux_ = portMUX_INITIALIZER_UNLOCKED;
  SettingsManager& settings_;
  String ssid_;
  String password_;
  bool wasConnected_ = false;
  uint32_t lastAttemptMs_ = 0;
  bool restartPending_ = false;
};
