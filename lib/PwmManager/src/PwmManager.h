#pragma once

#include <Adafruit_PWMServoDriver.h>
#include <Arduino.h>

class PwmManager {
 public:
  explicit PwmManager(uint8_t address);

  bool begin();
  bool setPercent(uint8_t channel, uint8_t percent);
  void disableChannel(uint8_t channel);
  bool isReady() const;

 private:
  static uint16_t percentToPwm(uint8_t percent);

  Adafruit_PWMServoDriver driver_;
  bool ready_ = false;
};

