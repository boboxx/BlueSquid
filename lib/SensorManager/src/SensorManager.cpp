#include "SensorManager.h"

#include <Arduino.h>
#include <math.h>

#include "AppConfig.h"
#include "Logging.h"

namespace {
constexpr char kTag[] = "Sensors";
}

SensorManager::SensorManager(EventManager& eventManager,
                             SettingsManager& settingsManager)
    : eventManager_(eventManager), settingsManager_(settingsManager) {}

bool SensorManager::begin() {
  startupMs_ = millis();
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
  return true;
}

void SensorManager::update() {
  const uint32_t now = millis();

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
  pitchFilter_.reset(calibratedPitch);
  rollFilter_.reset(calibratedRoll);
  status_.pitchDegrees = pitchFilter_.report(pitchZeroDegrees_, true);
  status_.rollDegrees = rollFilter_.report(rollZeroDegrees_, true);

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
  status_.pitchDegrees = pitchFilter_.report(pitchZeroDegrees_, true);
  status_.rollDegrees = rollFilter_.report(rollZeroDegrees_, true);
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
      pitchFilter_.update(sample.pitchDegrees, AppConfig::Sensors::kAttitudeFilterAlpha);
  absoluteRollDegrees_ =
      rollFilter_.update(sample.rollDegrees, AppConfig::Sensors::kAttitudeFilterAlpha);
  attitudeInitialized_ = true;
  status_.pitchDegrees = pitchFilter_.report(pitchZeroDegrees_);
  status_.rollDegrees = rollFilter_.report(rollZeroDegrees_);
  status_.valid = true;

  const uint32_t now = millis();
  if (now - lastAttitudeLogMs_ >=
      AppConfig::Sensors::kAttitudeLogIntervalMs) {
    lastAttitudeLogMs_ = now;
    LOG_INFO(kTag,
             "GY-61 avg: X=%.0f Y=%.0f Z=%.0f mV; raw P=%.2f R=%.2f; filtered P=%.2f R=%.2f; "
             "zero P=%.1f R=%.1f; reported P=%.1f R=%.1f",
             sample.xMv, sample.yMv, sample.zMv, sample.pitchDegrees, sample.rollDegrees, absolutePitchDegrees_,
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
