#pragma once

#include <Adafruit_HTU21DF.h>
#include <Wire.h>

#include "EventManager.h"
#include "SettingsManager.h"
#include "SystemTypes.h"
#include "LevelFilter.h"

class SensorManager {
 public:
  SensorManager(EventManager& eventManager, SettingsManager& settingsManager);

  bool begin();
  void update();
  bool calibrateLevel();
  bool setLevelCalibration(float pitchZeroDegrees, float rollZeroDegrees);
  const SensorStatus& status() const;
  float pitchZeroDegrees() const;
  float rollZeroDegrees() const;

 private:
  struct AttitudeSample {
    float xMv = 0.0F;
    float yMv = 0.0F;
    float zMv = 0.0F;
    float pitchDegrees = 0.0F;
    float rollDegrees = 0.0F;
  };

  void readEnvironmentalSensors();
  void readAttitude();
  AttitudeSample sampleAttitude() const;
  static float readAveragedMillivolts(int pin);

  EventManager& eventManager_;
  SettingsManager& settingsManager_;
  SensorStatus status_{};
  TwoWire climateWire_{1};
  Adafruit_HTU21DF htu21d_;
  bool htu21dReady_ = false;
  bool attitudeInitialized_ = false;
  LevelFilter pitchFilter_, rollFilter_;
  float absolutePitchDegrees_ = 0.0F;
  float absoluteRollDegrees_ = 0.0F;
  float pitchZeroDegrees_ = 0.0F;
  float rollZeroDegrees_ = 0.0F;
  uint32_t startupMs_ = 0;
  uint32_t lastAttitudeSampleMs_ = 0;
  uint32_t lastClimateSampleMs_ = 0;
  uint32_t lastAttitudeLogMs_ = 0;
  uint32_t climateSampleCount_ = 0;
};
