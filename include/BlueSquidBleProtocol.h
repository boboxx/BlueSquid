#pragma once

#include <Arduino.h>

#include "BlueSquidCanProtocol.h"

// BLE-native transport used by the dedicated touchscreen.  The legacy
// 7D8B100x service remains available for the iOS application.
namespace BlueSquidBle {

constexpr char kServiceUuid[] = "7D8B2000-8A75-4E41-9A6A-35D0A7A18B01";
constexpr char kSnapshotUuid[] = "7D8B2001-8A75-4E41-9A6A-35D0A7A18B01";
constexpr char kCommandUuid[] = "7D8B2002-8A75-4E41-9A6A-35D0A7A18B01";
constexpr char kAckUuid[] = "7D8B2003-8A75-4E41-9A6A-35D0A7A18B01";

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kSnapshotSize = 111;
constexpr size_t kMaximumCommandPayload = 12;
constexpr size_t kCommandHeaderSize = 6;
constexpr size_t kAckSize = 10;
constexpr uint32_t kOnlineTimeoutMs = 3500;
constexpr uint32_t kReconnectDelayMs = 1000;
constexpr uint32_t kScanDurationSeconds = 2;
constexpr uint32_t kInteractiveCoalesceMs = 40;

enum SnapshotOffset : size_t {
  kSnapshotVersion = 0,
  kSnapshotValidity = 1,
  kSnapshotRevision = 2,
  kSnapshotUptime = 6,
  kSnapshotFirmware = 10,
  kSnapshotOutputFlags = 13,
  kSnapshotFan = 17,
  kSnapshotFrontRgbw = 21,
  kSnapshotRearRgbw = 25,
  kSnapshotFrontPreset = 29,
  kSnapshotRearPreset = 34,
  kSnapshotVoltage = 39,
  kSnapshotCurrent = 41,
  kSnapshotSoc = 43,
  kSnapshotBatteryPower = 45,
  kSnapshotSolarPower = 47,
  kSnapshotDcDcPower = 49,
  kSnapshotLoadPower = 51,
  kSnapshotRemainingAh = 53,
  kSnapshotTimeToGo = 55,
  kSnapshotChargerStates = 57,
  kSnapshotCabinTemperature = 59,
  kSnapshotFridgeTemperature = 61,
  kSnapshotHumidity = 63,
  kSnapshotPitch = 65,
  kSnapshotRoll = 67,
  kSnapshotBatteryCapacity = 69,
  kSnapshotPitchZero = 71,
  kSnapshotRollZero = 73,
  kSnapshotEnergyTotals = 75,
  kSnapshotReserved = 81,
  kSnapshotInverterMode = 81,
  kSnapshotSp630eAssigned = 82,
  kSnapshotSp630eAvailable = 83,
  kSnapshotShorePower = 84,
  kSnapshotShoreState = 86,
  kSnapshotShoreValid = 87,
  kSnapshotFanFlags = 106,
  kSnapshotFanPreset = 107,
  kSnapshotFanSource = 108,
  kSnapshotFanInstance = 109,
  kSnapshotFanError = 110,
  kSnapshotExtraRgbw = 88, // two 9-byte RGBW output/preset records
};

enum ValidityFlag : uint8_t {
  kBatteryValid = 1 << 0,
  kShuntValid = 1 << 1,
  kSolarValid = 1 << 2,
  kDcDcValid = 1 << 3,
  kSensorsValid = 1 << 4,
  kInverterValid = 1 << 5,
};

enum class AckResult : uint8_t {
  Accepted = 0,
  InvalidCommand = 1,
  InvalidTarget = 2,
  InvalidValue = 3,
  Malformed = 4,
  Busy = 5,
};

inline void writeU16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8U);
}

inline void writeI16(uint8_t* destination, int16_t value) {
  writeU16(destination, static_cast<uint16_t>(value));
}

inline void writeU32(uint8_t* destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8U);
  destination[2] = static_cast<uint8_t>(value >> 16U);
  destination[3] = static_cast<uint8_t>(value >> 24U);
}

inline uint16_t readU16(const uint8_t* source) {
  return static_cast<uint16_t>(source[0]) |
         (static_cast<uint16_t>(source[1]) << 8U);
}

inline int16_t readI16(const uint8_t* source) {
  return static_cast<int16_t>(readU16(source));
}

inline uint32_t readU32(const uint8_t* source) {
  return static_cast<uint32_t>(source[0]) |
         (static_cast<uint32_t>(source[1]) << 8U) |
         (static_cast<uint32_t>(source[2]) << 16U) |
         (static_cast<uint32_t>(source[3]) << 24U);
}

inline size_t encodeValueCommand(uint16_t sequence,
                                 BlueSquidCan::Command command,
                                 uint8_t target, uint16_t value,
                                 uint8_t* output) {
  if (output == nullptr) return 0;
  output[0] = kProtocolVersion;
  writeU16(output + 1, sequence);
  output[3] = static_cast<uint8_t>(command);
  output[4] = target;
  output[5] = 2;
  writeU16(output + kCommandHeaderSize, value);
  return kCommandHeaderSize + 2;
}

}  // namespace BlueSquidBle
