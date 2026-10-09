#pragma once

#include <Arduino.h>

#include "VictronAlerts.h"

struct TouchRemoteStatus {
  float voltage = 0;
  float current = 0;
  float soc = 0;
  int16_t batteryPower = 0;
  uint16_t solarPower = 0;
  uint16_t dcDcPower = 0;
  uint16_t shorePower = 0;
  uint8_t shoreState = 255;
  bool shoreValid = false;
  uint16_t loadPower = 0;
  float remainingAh = 0;
  uint16_t timeToGoMinutes = 0;
  float cabinTemperatureC = 0;
  float fridgeTemperatureC = 0;
  float humidity = 0;
  float pitchDegrees = 0;
  float rollDegrees = 0;
  uint8_t fan = 0;
  uint8_t fanFlags = 0, fanPreset = 50, fanSource = 159, fanInstance = 1, fanError = 0;
  uint8_t sp630eAssigned = 0;
  uint8_t sp630eAvailable = 0;
  uint8_t rgbw[4][4]{};
  const uint8_t* rgbwChannels(uint8_t zone) const { return rgbw[zone]; }
  uint8_t* rgbwChannels(uint8_t zone) { return rgbw[zone]; }
  uint8_t rgb[4][3]{{100, 0, 0}, {100, 0, 0}, {100, 0, 0}, {100, 0, 0}};
  uint8_t rgbwBrightness[4]{100, 100, 100, 100};
  uint8_t rgbwOptions[4]{2, 2, 2, 2};
  bool rgbwPresetValid[4]{};
  float batteryCapacityAh = 0;
  float pitchZeroDegrees = 0;
  float rollZeroDegrees = 0;
  bool settingsValid = false;
  uint8_t solarState = 255;
  uint8_t dcDcState = 255;
  bool usb = false;
  bool pump = false;
  bool accessory3 = false;
  bool accessory4 = false;
  bool energyValid = false;
  bool inverterValid = false;
  uint8_t inverterMode = 0;
  bool levelValid = false;
  bool systemInfoValid = false;
  uint8_t rearFirmwareMajor = 0;
  uint8_t rearFirmwareMinor = 0;
  uint8_t rearFirmwarePatch = 0;
  uint32_t rearUptimeSeconds = 0;
  // Cerbo UTC clock from the latest snapshot (0 = unknown), received at
  // lastHeartbeatMs.
  uint32_t cerboTime = 0;
  uint32_t lastHeartbeatMs = 0;
  // Victron alerts from the optional alert characteristic; invalid when the
  // Controller predates it or has no current Cerbo data.
  VictronAlerts::Snapshot alerts;
  bool anyLightsEnabled(uint8_t group = 15) const {
    for (uint8_t zone = 0; zone < 4; ++zone) {
      // Unassigned RGB slots can contain stale values after reassignment.
      if (!(group & (1U << zone)) || !(sp630eAssigned & (1U << zone))) continue;
      for (uint8_t channel = 0; channel < 4; ++channel)
        if (rgbwChannels(zone)[channel]) return true;
    }
    return false;
  }
  bool outputAvailable(uint8_t target) const {
    return target < 8 && (!(sp630eAssigned & (1U << target)) ||
                          (sp630eAvailable & (1U << target)));
  }
};
