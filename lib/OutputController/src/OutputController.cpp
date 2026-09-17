#include "OutputController.h"

#include <Arduino.h>

#include "AppConfig.h"
#include "Logging.h"

namespace {
constexpr char kTag[] = "Outputs";
constexpr uint8_t kRgbwFirstPwmChannel = 1;
constexpr uint8_t kRgbwPwmResolutionBits = 8;
constexpr uint8_t kRgbwChannelCount = 4;
constexpr uint8_t kRgbwZoneCount = 4;

}  // namespace

OutputController::OutputController(PwmManager& pwmManager,
                                   EventManager& eventManager,
                                   SettingsManager& settingsManager)
    : pwmManager_(pwmManager),
      eventManager_(eventManager),
      settingsManager_(settingsManager) {}

void OutputController::begin() {
  configureOutputPin(AppConfig::Outputs::kUsbEnablePin);
  configureOutputPin(AppConfig::Outputs::kWaterPumpPin);
  configureAccessory3Output();
  configureAccessory4Output();
  configureRgbwOutputs();
  restoreDeviceState();
  lightGroupMask_ = settingsManager_.loadLightGroupMask();
  LOG_INFO(kTag, "Output abstraction initialized");
}

void OutputController::update() {
  const uint32_t now = millis();
  for (uint8_t zone = 0; zone < kRgbwZoneCount; ++zone) {
    if (rgbwPresetPending_[zone] &&
        now - rgbwLastCommandMs_[zone] >=
            AppConfig::kRgbwCommandSettleMs) {
      rgbwPresetPending_[zone] = false;
      updateRgbwPresetFromOutputs(zone);
    }
  }

  if (savePending_ &&
      now - saveRequestedMs_ >= AppConfig::kDeviceStateSaveDelayMs) {
    if (settingsManager_.saveDeviceState(deviceState_)) {
      savePending_ = false;
    } else {
      saveRequestedMs_ = now;
    }
  }
}

void OutputController::setRgbwExternal(uint8_t zone, bool external) {
  if (zone < 4) externalRgbw_[zone] = external;
}

bool OutputController::rgbwLightConfigured(uint8_t zone) const {
  if (zone >= 4) return false;
  if (externalRgbw_[zone]) return true;
  if (zone >= 2) return false;
  const int* pins = zone == 0 ? AppConfig::Outputs::kFrontRgbwPins : AppConfig::Outputs::kBedRgbwPins;
  for (uint8_t channel = 0; channel < 4; ++channel) if (pins[channel] >= 0) return true;
  return false;
}

void OutputController::setAllLightsEnabled(bool enabled) {
  const uint8_t level = enabled ? 100 : 0;
  for (uint8_t target = 0; target < 4; ++target) {
    if (!(lightGroupMask_ & (1U << target))) continue;
    if (rgbwLightConfigured(target)) {
      const uint8_t channels[4] = {0, 0, 0, level};
      const uint8_t white = deviceState_.rgbwOptions[target] & 6;
      setRgbwState(static_cast<RgbwZone>(target), channels, deviceState_.rgb[target], 100, white ? white : 2);
    }
  }
}
bool OutputController::anyLightsEnabled() const {
  for (uint8_t target = 0; target < 4; ++target) {
    if (!(lightGroupMask_ & (1U << target))) continue;
    if (rgbwLightConfigured(target)) {
      const uint8_t* channels = status_.rgbw[target];
      for (uint8_t i = 0; i < 4; ++i) if (channels[i]) return true;
    }
  }
  return false;
}

bool OutputController::setRgbwChannel(RgbwZone zone, uint8_t channel,
                                      uint8_t percent) {
  if (channel >= kRgbwChannelCount || static_cast<uint8_t>(zone) >= 4) {
    return false;
  }

  const uint8_t value = constrain(percent, 0, 100);
  writeRgbwChannel(zone, channel, value);
  status_.rgbw[static_cast<uint8_t>(zone)][channel] = value;
  if (!restoring_) {
    const uint8_t zoneIndex = static_cast<uint8_t>(zone);
    deviceState_.rgbwLevel[zoneIndex][channel] = value;
    rgbwPresetPending_[zoneIndex] = true;
    rgbwLastCommandMs_[zoneIndex] = millis();
    scheduleSave();
  }
  publishChange();
  return true;
}

bool OutputController::setRgbwPresetField(RgbwZone zone, uint8_t field,
                                          uint8_t value) {
  const uint8_t zoneIndex = static_cast<uint8_t>(zone);
  if (zoneIndex >= kRgbwZoneCount || field > 4) return false;
  if ((field <= 3 && value > 100) || (field == 4 && value > 7)) {
    return false;
  }

  if (field < 3) {
    deviceState_.rgb[zoneIndex][field] = value;
  } else if (field == 3) {
    deviceState_.rgbwBrightness[zoneIndex] = value;
  } else {
    deviceState_.rgbwOptions[zoneIndex] = value;
  }
  rgbwPresetPending_[zoneIndex] = false;
  syncPresetStatus();
  scheduleSave();
  return true;
}

bool OutputController::setRgbwState(RgbwZone zone, const uint8_t channels[4],
                                    const uint8_t color[3],
                                    uint8_t brightness, uint8_t options) {
  if (static_cast<uint8_t>(zone) >= kRgbwZoneCount || channels == nullptr || color == nullptr || brightness > 100 ||
      options > 7) {
    return false;
  }
  for (uint8_t index = 0; index < 4; ++index) {
    if (channels[index] > 100) return false;
  }
  for (uint8_t index = 0; index < 3; ++index) {
    if (color[index] > 100) return false;
  }
  for (uint8_t index = 0; index < 4; ++index) {
    if (!setRgbwChannel(zone, index, channels[index])) return false;
  }
  for (uint8_t index = 0; index < 3; ++index) {
    if (!setRgbwPresetField(zone, index, color[index])) return false;
  }
  if (!setRgbwPresetField(zone, 3, brightness) ||
      !setRgbwPresetField(zone, 4, options)) {
    return false;
  }
  return true;
}

void OutputController::setAllRgbwWhite(bool enabled) {
  const uint8_t channels[4] = {0, 0, 0, static_cast<uint8_t>(enabled ? 100 : 0)};
  for (uint8_t index = 0; index < kRgbwZoneCount; ++index) {
    if (!rgbwLightConfigured(index)) continue;
    // Preserve the assigned warm/cool white choice; default to warm white.
    const uint8_t whiteOptions = deviceState_.rgbwOptions[index] & 6;
    setRgbwState(static_cast<RgbwZone>(index), channels, deviceState_.rgb[index],
                 100, whiteOptions ? whiteOptions : 2);
  }
}

bool OutputController::rgbwLightsEnabled() const {
  for (uint8_t zone = 0; zone < kRgbwZoneCount; ++zone) {
    if (!rgbwLightConfigured(zone)) continue;
    for (uint8_t level : status_.rgbw[zone]) if (level) return true;
  }
  return false;
}

namespace {
void setRgbw(OutputController& controller, RgbwZone zone, uint8_t red,
             uint8_t green, uint8_t blue, uint8_t white) {
  const uint8_t values[] = {red, green, blue, white};
  for (uint8_t channel = 0; channel < kRgbwChannelCount; ++channel) {
    controller.setRgbwChannel(zone, channel, values[channel]);
  }
}
}  // namespace

void OutputController::applyScene(LightingScene scene) {
  switch (scene) {
    case LightingScene::Day:
      setRgbw(*this, RgbwZone::Output3, 0, 0, 0, 100);
      setRgbw(*this, RgbwZone::Output4, 0, 0, 0, 70);
      setRgbw(*this, RgbwZone::Output1, 0, 0, 0, 100);
      setRgbw(*this, RgbwZone::Output2, 0, 0, 0, 80);
      break;
    case LightingScene::Camp:
      setRgbw(*this, RgbwZone::Output3, 0, 0, 0, 45);
      setRgbw(*this, RgbwZone::Output4, 0, 0, 0, 60);
      setRgbw(*this, RgbwZone::Output1, 60, 20, 0, 15);
      setRgbw(*this, RgbwZone::Output2, 45, 10, 0, 10);
      break;
    case LightingScene::Night:
      setRgbw(*this, RgbwZone::Output3, 0, 0, 0, 8);
      setRgbw(*this, RgbwZone::Output4, 0, 0, 0, 15);
      setRgbw(*this, RgbwZone::Output1, 5, 0, 0, 0);
      setRgbw(*this, RgbwZone::Output2, 3, 0, 0, 0);
      break;
    case LightingScene::Travel:
      setRgbw(*this, RgbwZone::Output3, 0, 0, 0, 0);
      setRgbw(*this, RgbwZone::Output4, 0, 0, 0, 0);
      setRgbw(*this, RgbwZone::Output1, 0, 0, 0, 0);
      setRgbw(*this, RgbwZone::Output2, 0, 0, 0, 0);
      break;
  }
}

void OutputController::setRvcFanStatus(bool enabled, bool online, bool on, uint8_t speed,
                                       bool pending, uint8_t source, uint8_t instance, uint8_t error, uint8_t direction) {
  status_.fanFlags = (enabled ? 1 : 0) | (online ? 2 : 0) | (on ? 4 : 0) | (pending ? 8 : 0) | (direction == 1 ? 16 : 0) | (direction <= 1 ? 32 : 0);
  status_.fanSpeed = on ? speed : 0;
  if (speed) status_.fanPreset = speed;
  status_.fanSource = source; status_.fanInstance = instance; status_.fanError = error;
}

bool OutputController::setFanSpeed(uint8_t percent) {
  if (fanCommand_) return fanCommand_(percent);

  const uint8_t value = constrain(percent, 0, 100);
  if (!pwmManager_.setPercent(AppConfig::Pwm::kOutput4Channel, value)) {
    return false;
  }
  status_.fanSpeed = value;
  if (!restoring_) {
    deviceState_.fanSpeed = value;
    scheduleSave();
  }
  publishChange();
  return true;
}

void OutputController::setUsbEnabled(bool enabled) {
  status_.usbEnabled = enabled;
  writeOutputPin(AppConfig::Outputs::kUsbEnablePin, enabled);
  if (!restoring_) {
    deviceState_.usbEnabled = enabled ? 1 : 0;
    scheduleSave();
  }
  publishChange();
}

void OutputController::setWaterPumpEnabled(bool enabled) {
  status_.waterPumpEnabled = enabled;
  writeOutputPin(AppConfig::Outputs::kWaterPumpPin, enabled);
  if (!restoring_) {
    deviceState_.pumpRequested = enabled ? 1 : 0;
    scheduleSave();
  }
  publishChange();
}

void OutputController::setAccessory3Enabled(bool enabled) {
  status_.accessory3Enabled = enabled;
  writeAccessory3Output(enabled);
  if (!restoring_) {
    deviceState_.accessory3Requested = enabled ? 1 : 0;
    scheduleSave();
  }
  LOG_INFO(kTag, "Accessory output 3 %s", enabled ? "enabled" : "disabled");
  publishChange();
}

void OutputController::setAccessory4Enabled(bool enabled) {
  status_.accessory4Enabled = enabled;
  writeAccessory4Output(enabled);
  if (!restoring_) {
    deviceState_.accessory4Requested = enabled ? 1 : 0;
    scheduleSave();
  }
  LOG_INFO(kTag, "Accessory output 4 %s", enabled ? "enabled" : "disabled");
  publishChange();
}

const OutputStatus& OutputController::status() const {
  return status_;
}

void OutputController::restoreDeviceState() {
  if (!settingsManager_.loadDeviceState(deviceState_)) {
    LOG_INFO(kTag, "No saved device state; using safe defaults");
  }
  sanitizeDeviceState();
  syncPresetStatus();

  restoring_ = true;
  for (uint8_t zone = 0; zone < kRgbwZoneCount; ++zone) {
    for (uint8_t channel = 0; channel < kRgbwChannelCount; ++channel) {
      setRgbwChannel(static_cast<RgbwZone>(zone), channel,
                     deviceState_.rgbwLevel[zone][channel]);
    }
  }
  setFanSpeed(deviceState_.fanSpeed);
  setUsbEnabled(deviceState_.usbEnabled != 0);
  setWaterPumpEnabled(false);
  setAccessory3Enabled(false);
  setAccessory4Enabled(false);
  restoring_ = false;

  LOG_INFO(kTag,
           "Restored lights, ambience, fan and USB; pump and accessory 3 "
           "held off for safety");
}

void OutputController::sanitizeDeviceState() {
  for (uint8_t zone = 0; zone < kRgbwZoneCount; ++zone) {
    for (uint8_t channel = 0; channel < kRgbwChannelCount; ++channel) {
      deviceState_.rgbwLevel[zone][channel] =
          constrain(deviceState_.rgbwLevel[zone][channel], 0, 100);
    }
    for (uint8_t channel = 0; channel < 3; ++channel) {
      deviceState_.rgb[zone][channel] =
          constrain(deviceState_.rgb[zone][channel], 0, 100);
    }
    deviceState_.rgbwBrightness[zone] =
        constrain(deviceState_.rgbwBrightness[zone], 0, 100);
    deviceState_.rgbwOptions[zone] &= 0x07;
  }
  deviceState_.fanSpeed = constrain(deviceState_.fanSpeed, 0, 100);
  deviceState_.usbEnabled = deviceState_.usbEnabled ? 1 : 0;
  deviceState_.pumpRequested = deviceState_.pumpRequested ? 1 : 0;
  deviceState_.accessory3Requested = deviceState_.accessory3Requested ? 1 : 0;
  deviceState_.accessory4Requested = deviceState_.accessory4Requested ? 1 : 0;
}

void OutputController::updateRgbwPresetFromOutputs(uint8_t zone) {
  const uint8_t values[4] = {
      deviceState_.rgbwLevel[zone][0], deviceState_.rgbwLevel[zone][1],
      deviceState_.rgbwLevel[zone][2], deviceState_.rgbwLevel[zone][3]};
  const uint8_t colorLevel = max(values[0], max(values[1], values[2]));
  const uint8_t brightness = max(colorLevel, values[3]);
  if (brightness == 0) return;

  if (colorLevel != 0) {
    for (uint8_t channel = 0; channel < 3; ++channel) {
      deviceState_.rgb[zone][channel] = static_cast<uint8_t>(
          (static_cast<uint16_t>(values[channel]) * 100U +
           colorLevel / 2U) /
          colorLevel);
    }
  }
  deviceState_.rgbwBrightness[zone] = brightness;
  deviceState_.rgbwOptions[zone] =
      (colorLevel != 0 ? 1 : 0) | (values[3] != 0 ? 2 : 0);
  syncPresetStatus();
}

void OutputController::syncPresetStatus() {
  memcpy(status_.rgb, deviceState_.rgb, sizeof(status_.rgb));
  memcpy(status_.rgbwBrightness, deviceState_.rgbwBrightness,
         sizeof(status_.rgbwBrightness));
  memcpy(status_.rgbwOptions, deviceState_.rgbwOptions,
         sizeof(status_.rgbwOptions));
}

void OutputController::scheduleSave() {
  if (restoring_) return;
  savePending_ = true;
  saveRequestedMs_ = millis();
}

void OutputController::configureOutputPin(int pin) {
  if (pin >= 0) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
}

void OutputController::writeOutputPin(int pin, bool enabled) {
  if (pin >= 0) {
    digitalWrite(pin, enabled ? HIGH : LOW);
  }
}

void OutputController::configureAccessory3Output() {
  const int pin = AppConfig::Outputs::kAccessory3Pin;
  if (pin < 0) {
    LOG_INFO(kTag, "Accessory output 3 disabled");
    return;
  }

  pinMode(pin, OUTPUT);
  writeAccessory3Output(false);
  LOG_INFO(kTag, "Accessory output 3 GPIO %d initialized (%s)",
           pin,
           AppConfig::Outputs::kAccessory3ActiveHigh
               ? "active high"
               : "active low");
}

void OutputController::writeAccessory3Output(bool enabled) {
  const int pin = AppConfig::Outputs::kAccessory3Pin;
  if (pin < 0) {
    return;
  }

  const bool outputHigh =
      AppConfig::Outputs::kAccessory3ActiveHigh ? enabled : !enabled;
  digitalWrite(pin, outputHigh ? HIGH : LOW);
}

void OutputController::configureAccessory4Output() {
  const int pin = AppConfig::Outputs::kAccessory4Pin;
  if (pin < 0) {
    LOG_INFO(kTag, "Accessory output 4 disabled");
    return;
  }
  pinMode(pin, OUTPUT);
  writeAccessory4Output(false);
  LOG_INFO(kTag, "Accessory output 4 GPIO %d initialized (%s)", pin,
           AppConfig::Outputs::kAccessory4ActiveHigh ? "active high" : "active low");
}

void OutputController::writeAccessory4Output(bool enabled) {
  const int pin = AppConfig::Outputs::kAccessory4Pin;
  if (pin < 0) return;
  const bool outputHigh =
      AppConfig::Outputs::kAccessory4ActiveHigh ? enabled : !enabled;
  digitalWrite(pin, outputHigh ? HIGH : LOW);
}

void OutputController::configureRgbwOutputs() {
  for (uint8_t zone = 0; zone < 2; ++zone) {
    for (uint8_t channel = 0; channel < kRgbwChannelCount; ++channel) {
      const int pin = zone == 0 ? AppConfig::Outputs::kFrontRgbwPins[channel]
                                : AppConfig::Outputs::kBedRgbwPins[channel];
      if (pin < 0) {
        continue;
      }
      const uint8_t pwmChannel =
          kRgbwFirstPwmChannel + zone * kRgbwChannelCount + channel;
      ledcSetup(pwmChannel, AppConfig::Outputs::kRgbwPwmFrequencyHz,
                kRgbwPwmResolutionBits);
      ledcAttachPin(pin, pwmChannel);
      writeRgbwChannel(static_cast<RgbwZone>(zone), channel, 0);
    }
  }
  LOG_INFO(kTag, "Front RGBW pins: R=%d G=%d B=%d W=%d",
           AppConfig::Outputs::kFrontRgbwPins[0],
           AppConfig::Outputs::kFrontRgbwPins[1],
           AppConfig::Outputs::kFrontRgbwPins[2],
           AppConfig::Outputs::kFrontRgbwPins[3]);
  LOG_INFO(kTag, "Bed RGBW pins: R=%d G=%d B=%d W=%d",
           AppConfig::Outputs::kBedRgbwPins[0],
           AppConfig::Outputs::kBedRgbwPins[1],
           AppConfig::Outputs::kBedRgbwPins[2],
           AppConfig::Outputs::kBedRgbwPins[3]);
}

void OutputController::writeRgbwChannel(RgbwZone zone, uint8_t channel,
                                       uint8_t percent) {
  if (channel >= kRgbwChannelCount) {
    return;
  }
  const uint8_t zoneIndex = static_cast<uint8_t>(zone);
  if (zoneIndex >= 2) return;
  const int pin = zone == RgbwZone::Output1
                      ? AppConfig::Outputs::kFrontRgbwPins[channel]
                      : AppConfig::Outputs::kBedRgbwPins[channel];
  if (pin < 0) {
    return;
  }
  uint8_t duty = static_cast<uint8_t>(
      (static_cast<uint16_t>(constrain(percent, 0, 100)) * 255U) / 100U);
  if (!AppConfig::Outputs::kRgbwActiveHigh) {
    duty = 255U - duty;
  }
  ledcWrite(kRgbwFirstPwmChannel + zoneIndex * kRgbwChannelCount + channel,
            duty);
  LOG_DEBUG(kTag, "RGBW zone %u channel %u GPIO %d = %u%%", zoneIndex,
            channel, pin, percent);
}

void OutputController::publishChange() const {
  eventManager_.publish({EventType::OutputChanged, millis()});
}
