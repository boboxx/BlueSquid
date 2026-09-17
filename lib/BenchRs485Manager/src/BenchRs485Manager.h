#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include "BatteryManager.h"
#include "BlueSquidSerialProtocol.h"
#include "OutputController.h"
#include "SensorManager.h"
#include "SystemTypes.h"

class BenchRs485Manager {
 public:
  BenchRs485Manager(OutputController& outputs, BatteryManager& battery,
                    SensorManager& sensors);

  bool begin();
  void update();
  void publishStatus(const SystemStatus& status);
  bool connected() const;
  uint32_t receivedByteCount() const { return receivedByteCount_; }
  uint32_t validFrameCount() const { return validFrameCount_; }

 private:
  bool transmit(uint16_t id, const uint8_t* data, uint8_t length);
  void processCommand(const BlueSquidSerial::Frame& frame);
  void acknowledge(uint8_t sequence, uint8_t command, uint8_t result);

  OutputController& outputs_;
  BatteryManager& battery_;
  SensorManager& sensors_;
  HardwareSerial serial_{1};
  BlueSquidSerial::Parser parser_{};
  uint32_t lastCommandMs_ = 0;
  uint32_t receivedByteCount_ = 0;
  uint32_t validFrameCount_ = 0;
  uint8_t statusSequence_ = 0;
  bool initialized_ = false;
};
