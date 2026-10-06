#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "Sp630eProtocol.h"

#include "RgbwBleDriverManager.h"

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;

// Adapter for BanlanX SP630E controllers using service FFE0 (or E0FF) and
// writable characteristic FFE1. Supports RGBW and 5CH PWM RGBCCT with RGB
// and WW wired, CW unused. The status reply selects the command mapping.
class Sp630eBleAdapter final : public RgbwBleDriverAdapter {
 public:
  // An empty address is discovery-only and never opens a connection.
  Sp630eBleAdapter(const char* label, const char* address = "");
  void setAddress(const char* address);
  const String& address() const { return configuredAddress_; }

  void begin() override;
  void update() override;
  bool ready() const override;
  bool send(const RgbwBleDriverState& state) override;

  bool available() const override;
  bool reported(RgbwBleDriverState& state, uint32_t& revision) const override;
  void disconnected();

  static bool requestDiscovery();
  static uint8_t discoveredCount();
  static String discoveredAddress(uint8_t index);
  static int discoveredRssi(uint8_t index);

 private:
  static void taskEntry(void* context);
  void taskLoop();
  bool connect();
  bool write(const uint8_t* data, size_t length);
  bool sendCommands(const RgbwBleDriverState& state);
  void notification(uint8_t* data, size_t length);

  const char* label_;
  String configuredAddress_;
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* command_ = nullptr;
  TaskHandle_t task_ = nullptr;
  mutable portMUX_TYPE mutex_ = portMUX_INITIALIZER_UNLOCKED;
  RgbwBleDriverState desired_{};
  RgbwBleDriverState reported_{};
  uint32_t desiredRevision_ = 0;
  uint32_t desiredQueuedMs_ = 0;
  uint32_t sentRevision_ = 0;
  uint32_t reportRevision_ = 0;
  uint32_t reportCommandRevision_ = 0;
  uint32_t lastReportMs_ = 0;
  uint32_t notificationCount_ = 0;
  uint32_t decodedCount_ = 0;
  uint8_t configuration_ = 0;
  Sp630eProtocol::ResponseHealth responseHealth_;
  Sp630eProtocol::WarmCommandCache warmCache_;
  bool servicesLogged_ = false;
  bool queryPending_ = false;
  bool reportValid_ = false;
  bool acceptReport_ = false;
};
