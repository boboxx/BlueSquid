#pragma once

#include <Preferences.h>
#include <atomic>
#include <WString.h>
#include "RvcFanProtocol.h"

struct PersistentDeviceState {
  uint8_t rgbwLevel[4][4]{};
  uint8_t rgb[4][3]{{100, 0, 0}, {100, 0, 0}, {100, 0, 0}, {100, 0, 0}};
  uint8_t rgbwBrightness[4]{100, 100, 100, 100};
  // Bits 0/1/2: colour, warm white, cool white selected.
  uint8_t rgbwOptions[4]{2, 2, 2, 2};
  uint8_t fanSpeed = 0;
  uint8_t usbEnabled = 0;
  uint8_t pumpRequested = 0;
  uint8_t accessory3Requested = 0;
  uint8_t accessory4Requested = 0;
};

#include "Sp630eConfiguration.h"

class SettingsManager {
 public:
  bool begin();
  uint32_t rvcFanRevision() const { return rvcFanRevision_.load(); }
  RvcFan::Config loadRvcFanConfiguration();
  bool saveRvcFanConfiguration(const RvcFan::Config& config);
  void end();
  bool loadLevelCalibration(float& pitchZeroDegrees,
                            float& rollZeroDegrees);
  bool saveLevelCalibration(float pitchZeroDegrees,
                            float rollZeroDegrees);
  float loadBatteryCapacityAh(float defaultCapacityAh);
  bool saveBatteryCapacityAh(float capacityAh);
  String loadJkBmsAddresses(const char* defaultAddresses);
  bool saveJkBmsAddresses(const String& addresses);
  bool loadSp630eAssignments(
      Sp630eAssignment (&assignments)[kSp630eAssignmentCount]);
  bool saveSp630eAssignments(
      const Sp630eAssignment (&assignments)[kSp630eAssignmentCount]);
  uint8_t loadLightGroupMask();
  bool saveSp630eConfiguration(const Sp630eAssignment (&assignments)[8], uint8_t group);
  String loadCerboStationSsid(const char* defaultValue);
  String loadCerboStationPassword(const char* defaultValue);
  bool saveCerboStation(const String& ssid, const String& password);
  uint8_t loadCerboVebusUnitId(uint8_t defaultValue);
  bool saveCerboVebusUnitId(uint8_t unitId);
  bool loadDeviceState(PersistentDeviceState& state);
  bool saveDeviceState(const PersistentDeviceState& state);

 private:
  std::atomic<uint32_t> rvcFanRevision_{0};
  Preferences preferences_;
  bool ready_ = false;
};
