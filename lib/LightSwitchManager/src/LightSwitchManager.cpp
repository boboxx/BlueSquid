#include "LightSwitchManager.h"

#include "AppConfig.h"
#include "Logging.h"

namespace {
constexpr char kTag[] = "LightSwitch";
}

LightSwitchManager::LightSwitchManager(OutputController& outputController)
    : outputController_(outputController) {}

void LightSwitchManager::begin() {
  const int pin = AppConfig::Outputs::kAllLightsSwitchPin;
  if (pin < 0) {
    LOG_INFO(kTag, "Physical all-lights button disabled");
    return;
  }

  pinMode(pin, AppConfig::Outputs::kAllLightsSwitchActiveLow
                   ? INPUT_PULLUP
                   : INPUT_PULLDOWN);
  rawPressed_ = readPressed();
  stablePressed_ = rawPressed_;
  initialized_ = true;
  rawStateChangedMs_ = millis();
  lightsEnabled_ = false;
  LOG_INFO(kTag, "All-lights momentary button initialized on GPIO %d", pin);
}

bool LightSwitchManager::update() {
  if (!initialized_) {
    return false;
  }

  const bool currentPressed = readPressed();
  const uint32_t now = millis();
  if (currentPressed != rawPressed_) {
    rawPressed_ = currentPressed;
    rawStateChangedMs_ = now;
  }

  if (rawPressed_ != stablePressed_ &&
      now - rawStateChangedMs_ >=
          AppConfig::Outputs::kAllLightsSwitchDebounceMs) {
    stablePressed_ = rawPressed_;
    if (stablePressed_) {
      // Derive the next state from the outputs so touchscreen changes and the
      // physical button cannot leave this local toggle state out of sync.
      lightsEnabled_ = !outputController_.anyLightsEnabled();
      apply(lightsEnabled_);
      return true;
    }
  }
  return false;
}

bool LightSwitchManager::readPressed() const {
  const bool pinHigh =
      digitalRead(AppConfig::Outputs::kAllLightsSwitchPin) == HIGH;
  return AppConfig::Outputs::kAllLightsSwitchActiveLow ? !pinHigh : pinHigh;
}

void LightSwitchManager::apply(bool enabled) {
  outputController_.setAllLightsEnabled(enabled);
  LOG_INFO(kTag, "GPIO %d pressed: all lights %s", AppConfig::Outputs::kAllLightsSwitchPin,
           enabled ? "ON at 100%" : "OFF");
}
