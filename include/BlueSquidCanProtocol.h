#pragma once

#include <Arduino.h>

namespace BlueSquidCan {

constexpr uint8_t kProtocolVersion = 1;
constexpr uint32_t kBitRate = 250000;
constexpr uint32_t kStatusTimeoutMs = 3000;

enum Id : uint32_t {
  kHeartbeat = 0x100,
  kSystemInfo = 0x101,
  kBattery = 0x110,
  kPower = 0x111,
  kCapacity = 0x112,
  kClimate = 0x120,
  kLevel = 0x121,
  kOutputs = 0x130,
  kFrontRgbw = 0x131,
  kBedRgbw = 0x132,
  kFrontRgbwPreset = 0x133,
  kBedRgbwPreset = 0x134,
  kSettings = 0x135,
  kRgbw3 = 0x136,
  kRgbw4 = 0x137,
  kRgbw3Preset = 0x138,
  kRgbw4Preset = 0x139,
  kCommand = 0x200,
  kCommandAck = 0x201,
};

constexpr uint32_t kRgbwOutputIds[4] = {kFrontRgbw, kBedRgbw, kRgbw3, kRgbw4};
constexpr uint32_t kRgbwPresetIds[4] = {kFrontRgbwPreset, kBedRgbwPreset, kRgbw3Preset, kRgbw4Preset};

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
};

enum class Ack : uint8_t { Accepted = 0, InvalidCommand = 1, InvalidValue = 2 };

inline void writeU16(uint8_t* data, size_t offset, uint16_t value) {
  data[offset] = static_cast<uint8_t>(value);
  data[offset + 1] = static_cast<uint8_t>(value >> 8);
}

inline void writeI16(uint8_t* data, size_t offset, int16_t value) {
  writeU16(data, offset, static_cast<uint16_t>(value));
}

inline uint16_t readU16(const uint8_t* data, size_t offset) {
  return static_cast<uint16_t>(data[offset]) |
         (static_cast<uint16_t>(data[offset + 1]) << 8);
}

inline int16_t readI16(const uint8_t* data, size_t offset) {
  return static_cast<int16_t>(readU16(data, offset));
}

inline int16_t scaled(float value, float multiplier) {
  const float converted = value * multiplier;
  return static_cast<int16_t>(constrain(converted, -32768.0F, 32767.0F));
}

}  // namespace BlueSquidCan
