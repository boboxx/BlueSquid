#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "EventManager.h"
#include "InverterModeRequest.h"
#include "SettingsManager.h"
#include "SystemTypes.h"

class BatteryManager {
 public:
  BatteryManager(EventManager& eventManager, SettingsManager& settingsManager);

  bool begin();
  bool startBackgroundTask();
  void update();
  BatteryStatus status() const;
  bool consumeStatusChanged();
  float capacityAh() const;
  bool setCapacityAh(float capacityAh);
  bool setInverterEnabled(bool enabled);
  bool setChargerEnabled(bool enabled);
  uint8_t vebusUnitId() const { return vebusUnitId_; }
  bool setVebusUnitId(uint8_t unitId);

 private:
  struct CerboPort;
  static void taskEntry(void* context);
  void taskLoop();
  void commitStatus(const BatteryStatus& status);
  bool requestInverterMode(bool inverter, bool enabled);
  void updateEnergyTotals(BatteryStatus& status, uint32_t now);

  EventManager& eventManager_;
  SettingsManager& settingsManager_;
  BatteryStatus status_{};
  bool statusChanged_ = false;
  uint32_t lastEnergyMs_ = 0;
  float capacityAh_ = 0.0F;
  TaskHandle_t taskHandle_ = nullptr;
  int8_t pendingInverterMode_ = -1;
  InverterModeRequest modeRequest_;
  volatile uint8_t vebusUnitId_ = 227;
  CerboPort* cerbo_ = nullptr;
  mutable portMUX_TYPE statusMux_ = portMUX_INITIALIZER_UNLOCKED;
};
