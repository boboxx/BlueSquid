#pragma once

#include "EventManager.h"
#include <functional>
#include "PwmManager.h"
#include "SettingsManager.h"
#include "SystemTypes.h"

class OutputController {
 public:
  OutputController(PwmManager& pwmManager, EventManager& eventManager,
                   SettingsManager& settingsManager);

  void begin();
  void update();
  void setRgbwExternal(uint8_t zone, bool external);
  void setAllLightsEnabled(bool enabled);
  void setLightGroupMask(uint8_t mask) { lightGroupMask_ = mask & 15; }
  bool anyLightsEnabled() const;
  bool setRgbwChannel(RgbwZone zone, uint8_t channel, uint8_t percent);
  bool setRgbwPresetField(RgbwZone zone, uint8_t field, uint8_t value);
  bool setRgbwState(RgbwZone zone, const uint8_t channels[4],
                    const uint8_t color[3], uint8_t brightness,
                    uint8_t options);
  void setAllRgbwWhite(bool enabled);
  bool rgbwLightsEnabled() const;
  void applyScene(LightingScene scene);
  bool setFanSpeed(uint8_t percent);
  bool setFanReverse(bool intake) { return fanDirectionCommand_ && fanDirectionCommand_(intake); }
  void setFanDirectionHandler(std::function<bool(bool)> handler) { fanDirectionCommand_ = handler; }
  void setFanCommandHandler(std::function<bool(uint8_t)> handler) { fanCommand_ = handler; }
  void setRvcFanStatus(bool enabled, bool online, bool on, uint8_t speed, bool pending,
                       uint8_t source, uint8_t instance, uint8_t error, uint8_t direction);

  void setUsbEnabled(bool enabled);
  void setWaterPumpEnabled(bool enabled);
  void setAccessory3Enabled(bool enabled);
  void setAccessory4Enabled(bool enabled);
  const OutputStatus& status() const;

 private:
  static void configureOutputPin(int pin);
  static void writeOutputPin(int pin, bool enabled);
  static void configureAccessory3Output();
  static void writeAccessory3Output(bool enabled);
  static void configureAccessory4Output();
  static void writeAccessory4Output(bool enabled);
  static void configureRgbwOutputs();
  static void writeRgbwChannel(RgbwZone zone, uint8_t channel,
                               uint8_t percent);
  bool rgbwLightConfigured(uint8_t zone) const;
  void restoreDeviceState();
  void sanitizeDeviceState();
  void updateRgbwPresetFromOutputs(uint8_t zone);
  void syncPresetStatus();
  void scheduleSave();
  void publishChange() const;

  PwmManager& pwmManager_;
  EventManager& eventManager_;
  SettingsManager& settingsManager_;
  OutputStatus status_{};
  std::function<bool(uint8_t)> fanCommand_;
  std::function<bool(bool)> fanDirectionCommand_;
  PersistentDeviceState deviceState_{};
  uint8_t lightGroupMask_ = 15;
  bool externalRgbw_[4]{};
  bool restoring_ = false;
  bool savePending_ = false;
  uint32_t saveRequestedMs_ = 0;
  bool rgbwPresetPending_[4]{};
  uint32_t rgbwLastCommandMs_[4]{};
};
