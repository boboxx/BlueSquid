#include "PwmManager.h"

#include <Wire.h>

#include "AppConfig.h"
#include "Logging.h"

namespace {
constexpr char kTag[] = "PWM";
constexpr uint16_t kPwmMaximum = 4095;
}

PwmManager::PwmManager(uint8_t address) : driver_(address, Wire) {}

bool PwmManager::begin() {
#if BLUESQUID_SIMULATED_HARDWARE
  ready_ = true;
  LOG_INFO(kTag, "Using simulated PWM outputs");
  return true;
#else
  ready_ = driver_.begin();
  if (!ready_) {
    LOG_ERROR(kTag, "PCA9685 not detected at 0x%02X", AppConfig::I2c::kPca9685Address);
    return false;
  }

  driver_.setOscillatorFrequency(27000000);
  driver_.setPWMFreq(AppConfig::Pwm::kFrequencyHz);
  LOG_INFO(kTag, "PCA9685 initialized");
  return true;
#endif
}

bool PwmManager::setPercent(uint8_t channel, uint8_t percent) {
  if (!ready_ || channel >= 16) {
    return false;
  }

  const uint8_t constrainedPercent = constrain(percent, 0, 100);
#if BLUESQUID_SIMULATED_HARDWARE
  LOG_DEBUG(kTag, "Simulated channel %u = %u%%", channel, constrainedPercent);
  return true;
#else
  if (constrainedPercent == 0) {
    driver_.setPWM(channel, 0, 0);
  } else if (constrainedPercent == 100) {
    driver_.setPWM(channel, 4096, 0);
  } else {
    driver_.setPWM(channel, 0, percentToPwm(constrainedPercent));
  }
  return true;
#endif
}

void PwmManager::disableChannel(uint8_t channel) {
#if BLUESQUID_SIMULATED_HARDWARE
  (void)channel;
#else
  if (ready_ && channel < 16) {
    driver_.setPWM(channel, 0, 0);
  }
#endif
}

bool PwmManager::isReady() const {
  return ready_;
}

uint16_t PwmManager::percentToPwm(uint8_t percent) {
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(percent) * kPwmMaximum) / 100U);
}
