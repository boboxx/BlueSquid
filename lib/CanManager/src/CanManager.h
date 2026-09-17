#pragma once

#include <Arduino.h>
#include <driver/twai.h>

#include "BatteryManager.h"
#include "OutputController.h"
#include "SensorManager.h"
#include "SystemTypes.h"

class CanManager {
 public:
  CanManager(OutputController& outputs, BatteryManager& battery,
             SensorManager& sensors);

  bool begin();
  void update();
  void publishStatus(const SystemStatus& status);
  bool connected() const;

 private:
  bool transmit(uint32_t id, const uint8_t* data, uint8_t length);
  void processCommand(const twai_message_t& message);
  void acknowledge(uint8_t sequence, uint8_t command, uint8_t result);

  OutputController& outputs_;
  BatteryManager& battery_;
  SensorManager& sensors_;
  uint32_t lastCommandMs_ = 0;
  uint8_t statusSequence_ = 0;
  bool initialized_ = false;
};
