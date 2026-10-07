#pragma once

#include <Arduino.h>

namespace BlueSquidControl {

constexpr uint8_t kProtocolVersion = 1;
enum class Command : uint8_t {
  SetRgbw = 2,
  SetFan = 3,
  SetUsb = 4,
  SetPump = 5,
  SetAccessory3 = 6,
  ApplyScene = 7,
  RequestStatus = 8,
  SetAllLights = 9,
  CalibrateLevel = 10,
  SetRgbwPreset = 11,
  SetBatteryCapacity = 12,
  SetLevelCalibration = 13,
  SetRgbwState = 15,
  SetAccessory4 = 16,
  SetInverter = 17,
  SetCharger = 18,
  SetFanReverse = 19,
  // Opens the Controller's Bluetooth pairing window (2 minutes) so another
  // touchscreen can pair. Only accepted from an already trusted touchscreen.
  OpenPairing = 20,
  // Erases all Controller settings and pairings, then restarts. The value
  // must equal kFactoryResetConfirmation so no stray command can trigger it.
  FactoryReset = 21,
};
constexpr uint16_t kFactoryResetConfirmation = 0xFAC7;

inline int16_t scaled(float value, float multiplier) {
  const float converted = value * multiplier;
  return static_cast<int16_t>(constrain(converted, -32768.0F, 32767.0F));
}

}  // namespace BlueSquidControl
