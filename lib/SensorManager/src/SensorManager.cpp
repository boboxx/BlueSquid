#include "SensorManager.h"

#include <Arduino.h>
#include <math.h>

#include "AppConfig.h"
#include "Logging.h"

namespace {
constexpr char kTag[] = "Sensors";

#ifndef BLUESQUID_SIMULATED_SENSORS
#define BLUESQUID_SIMULATED_SENSORS BLUESQUID_SIMULATED_HARDWARE
#endif

float filteredValue(float previous, float current, bool initialized) {
  if (!initialized) {
    return current;
  }
  return previous +
         AppConfig::Sensors::kAttitudeFilterAlpha * (current - previous);
}
}

SensorManager::SensorManager(EventManager& eventManager,
                             SettingsManager& settingsManager)
    : eventManager_(eventManager), settingsManager_(settingsManager) {}

bool SensorManager::begin() {
  startupMs_ = millis();
#if BLUESQUID_SIMULATED_SENSORS
  status_.valid = true;
  LOG_INFO(kTag, "Using simulated sensor data");
#else
  settingsManager_.loadLevelCalibration(pitchZeroDegrees_, rollZeroDegrees_);
  LOG_INFO(kTag, "Level calibration: pitch %.1f, roll %.1f",
           pitchZeroDegrees_, rollZeroDegrees_);

  climateWire_.begin(AppConfig::I2c::kClimateSdaPin,
                     AppConfig::I2c::kClimateSclPin,
                     AppConfig::I2c::kClimateFrequencyHz);
  htu21dReady_ = htu21d_.begin(&climateWire_);
  LOG_INFO(kTag, "HTU21D on SDA=%d SCL=%d: %s",
           AppConfig::I2c::kClimateSdaPin, AppConfig::I2c::kClimateSclPin,
           htu21dReady_ ? "detected" : "not detected");

  analogReadResolution(12);
  analogSetPinAttenuation(AppConfig::Sensors::kGy61XPin, ADC_11db);
  analogSetPinAttenuation(AppConfig::Sensors::kGy61YPin, ADC_11db);
  analogSetPinAttenuation(AppConfig::Sensors::kGy61ZPin, ADC_11db);
  LOG_INFO(kTag, "GY-61 analog inputs: X=%d Y=%d Z=%d",
           AppConfig::Sensors::kGy61XPin, AppConfig::Sensors::kGy61YPin,
           AppConfig::Sensors::kGy61ZPin);

  status_.valid = htu21dReady_;
#endif
  return true;
}

void SensorManager::update() {
  const uint32_t now = millis();

#if BLUESQUID_SIMULATED_SENSORS
  if (now - lastAttitudeSampleMs_ >=
      AppConfig::Sensors::kAttitudeSampleIntervalMs) {
    lastAttitudeSampleMs_ = now;
    const float seconds = now / 1000.0F;
    status_.pitchDegrees = 1.2F;
    status_.rollDegrees = -0.6F;
    status_.cabinTemperatureC = 21.5F + 0.4F * sinf(seconds / 30.0F);
    status_.fridgeTemperatureC = 3.4F;
    status_.cabinHumidityPercent = 45.0F;
    eventManager_.publish({EventType::StatusChanged, now});
  }
#else
  if (now - lastClimateSampleMs_ >=
      AppConfig::Sensors::kClimateSampleIntervalMs) {
    lastClimateSampleMs_ = now;
    readEnvironmentalSensors();
  }

  const bool attitudeSettled =
      now - startupMs_ >= AppConfig::Sensors::kAttitudeStartupSettleMs;
  if (attitudeSettled &&
      now - lastAttitudeSampleMs_ >=
          AppConfig::Sensors::kAttitudeSampleIntervalMs) {
    lastAttitudeSampleMs_ = now;
    readAttitude();
    eventManager_.publish({EventType::StatusChanged, now});
  }
#endif
}

const SensorStatus& SensorManager::status() const {
  return status_;
}

float SensorManager::pitchZeroDegrees() const {
  return pitchZeroDegrees_;
}

float SensorManager::rollZeroDegrees() const {
  return rollZeroDegrees_;
}

bool SensorManager::calibrateLevel() {
  if (!attitudeInitialized_) {
    LOG_WARN(kTag, "Cannot calibrate level before the first GY-61 sample");
    return false;
  }

  float pitchTotal = 0.0F;
  float rollTotal = 0.0F;
  for (uint8_t sampleIndex = 0;
       sampleIndex < AppConfig::Sensors::kCalibrationSampleCount;
       ++sampleIndex) {
    const AttitudeSample sample = sampleAttitude();
    pitchTotal += sample.pitchDegrees;
    rollTotal += sample.rollDegrees;
    delay(AppConfig::Sensors::kCalibrationSampleDelayMs);
  }

  const float calibratedPitch =
      pitchTotal / AppConfig::Sensors::kCalibrationSampleCount;
  const float calibratedRoll =
      rollTotal / AppConfig::Sensors::kCalibrationSampleCount;

  if (!settingsManager_.saveLevelCalibration(calibratedPitch,
                                             calibratedRoll)) {
    LOG_ERROR(kTag, "Level calibration was not applied because it could not be stored");
    return false;
  }

  pitchZeroDegrees_ = calibratedPitch;
  rollZeroDegrees_ = calibratedRoll;
  absolutePitchDegrees_ = calibratedPitch;
  absoluteRollDegrees_ = calibratedRoll;
  status_.pitchDegrees = 0.0F;
  status_.rollDegrees = 0.0F;

  LOG_INFO(kTag, "Level calibrated at pitch %.1f, roll %.1f",
           pitchZeroDegrees_, rollZeroDegrees_);
  eventManager_.publish({EventType::SettingsChanged, millis()});
  return true;
}

bool SensorManager::setLevelCalibration(float pitchZeroDegrees,
                                        float rollZeroDegrees) {
  if (!isfinite(pitchZeroDegrees) || !isfinite(rollZeroDegrees) ||
      pitchZeroDegrees < -180.0F || pitchZeroDegrees > 180.0F ||
      rollZeroDegrees < -180.0F || rollZeroDegrees > 180.0F) {
    return false;
  }
  if (!settingsManager_.saveLevelCalibration(pitchZeroDegrees,
                                             rollZeroDegrees)) {
    return false;
  }
  pitchZeroDegrees_ = pitchZeroDegrees;
  rollZeroDegrees_ = rollZeroDegrees;
  status_.pitchDegrees = absolutePitchDegrees_ - pitchZeroDegrees_;
  status_.rollDegrees = absoluteRollDegrees_ - rollZeroDegrees_;
  eventManager_.publish({EventType::SettingsChanged, millis()});
  return true;
}

void SensorManager::readEnvironmentalSensors() {
  bool valid = false;

  if (htu21dReady_) {
    const float temperature = htu21d_.readTemperature();
    const float humidity = htu21d_.readHumidity();
    if (isfinite(temperature) && isfinite(humidity)) {
      status_.cabinTemperatureC = temperature;
      status_.cabinHumidityPercent = constrain(humidity, 0.0F, 100.0F);
      valid = true;
    } else {
      LOG_WARN(kTag, "HTU21D read failed");
    }
  }

  status_.valid = valid;
  ++climateSampleCount_;
  if (climateSampleCount_ % 5 == 0) {
    LOG_INFO(kTag, "Cabin %.1f C, humidity %.1f%%",
             status_.cabinTemperatureC, status_.cabinHumidityPercent);
  }
}

void SensorManager::readAttitude() {
  const AttitudeSample sample = sampleAttitude();

  absolutePitchDegrees_ =
      filteredValue(absolutePitchDegrees_, sample.pitchDegrees,
                    attitudeInitialized_);
  absoluteRollDegrees_ =
      filteredValue(absoluteRollDegrees_, sample.rollDegrees,
                    attitudeInitialized_);
  attitudeInitialized_ = true;
  status_.pitchDegrees = absolutePitchDegrees_ - pitchZeroDegrees_;
  status_.rollDegrees = absoluteRollDegrees_ - rollZeroDegrees_;
  status_.valid = true;

  const uint32_t now = millis();
  if (now - lastAttitudeLogMs_ >=
      AppConfig::Sensors::kAttitudeLogIntervalMs) {
    lastAttitudeLogMs_ = now;
    LOG_INFO(kTag,
             "GY-61 avg: X=%.0f Y=%.0f Z=%.0f mV; absolute P=%.1f R=%.1f; "
             "zero P=%.1f R=%.1f; reported P=%.1f R=%.1f",
             sample.xMv, sample.yMv, sample.zMv, absolutePitchDegrees_,
             absoluteRollDegrees_, pitchZeroDegrees_, rollZeroDegrees_,
             status_.pitchDegrees, status_.rollDegrees);
  }
}

SensorManager::AttitudeSample SensorManager::sampleAttitude() const {
  AttitudeSample sample;
  sample.xMv = readAveragedMillivolts(AppConfig::Sensors::kGy61XPin);
  sample.yMv = readAveragedMillivolts(AppConfig::Sensors::kGy61YPin);
  sample.zMv = readAveragedMillivolts(AppConfig::Sensors::kGy61ZPin);

  const float xG =
      (sample.xMv - AppConfig::Sensors::kGy61XZeroMv) /
      AppConfig::Sensors::kGy61SensitivityMvPerG;
  const float yG =
      (sample.yMv - AppConfig::Sensors::kGy61YZeroMv) /
      AppConfig::Sensors::kGy61SensitivityMvPerG;
  const float zG =
      (sample.zMv - AppConfig::Sensors::kGy61ZZeroMv) /
      AppConfig::Sensors::kGy61SensitivityMvPerG;

  sample.pitchDegrees =
      atan2f(-xG, sqrtf(yG * yG + zG * zG)) * 180.0F / PI;
  sample.rollDegrees = atan2f(yG, zG) * 180.0F / PI;
  if (AppConfig::Sensors::kInvertPitch) {
    sample.pitchDegrees = -sample.pitchDegrees;
  }
  if (AppConfig::Sensors::kInvertRoll) {
    sample.rollDegrees = -sample.rollDegrees;
  }
  return sample;
}

float SensorManager::readAveragedMillivolts(int pin) {
  uint32_t totalMillivolts = 0;
  for (uint8_t index = 0;
       index < AppConfig::Sensors::kAdcSamplesPerReading; ++index) {
    totalMillivolts += analogReadMilliVolts(pin);
    delayMicroseconds(50);
  }
  return static_cast<float>(totalMillivolts) /
         AppConfig::Sensors::kAdcSamplesPerReading;
}
