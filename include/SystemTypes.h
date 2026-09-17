#pragma once

#include <Arduino.h>

enum class LightingScene : uint8_t {
  Day,
  Camp,
  Night,
  Travel,
};

enum class RgbwZone : uint8_t {
  Output1,
  Output2,
  Output3,
  Output4,
};

struct BatteryStatus {
  float voltage = 0.0F;
  float current = 0.0F;
  float power = 0.0F;
  float stateOfCharge = 0.0F;
  float solarPower = 0.0F;
  float dcDcPower = 0.0F;
  float shorePower = 0.0F;
  bool shoreValid = false;
  uint8_t shoreState = 255;
  float loadPower = 0.0F;
  float consumedAh = 0.0F;
  float remainingAh = 0.0F;
  float timeToGoMinutes = 0.0F;
  float solarEnergyWh = 0.0F;
  float dcDcEnergyWh = 0.0F;
  float loadEnergyWh = 0.0F;
  uint8_t solarChargerState = 255;
  uint8_t dcDcChargerState = 255;
  uint8_t inverterMode = 0;
  bool inverterValid = false;
  bool shuntValid = false;
  bool solarValid = false;
  bool dcDcValid = false;
  bool valid = false;
};

struct SensorStatus {
  float cabinTemperatureC = 0.0F;
  float fridgeTemperatureC = 0.0F;
  float cabinHumidityPercent = 0.0F;
  float pitchDegrees = 0.0F;
  float rollDegrees = 0.0F;
  bool valid = false;
};

struct OutputStatus {
  uint8_t rgbw[4][4]{};
  uint8_t rgb[4][3]{{100, 0, 0}, {100, 0, 0}, {100, 0, 0}, {100, 0, 0}};
  uint8_t rgbwBrightness[4]{100, 100, 100, 100};
  uint8_t rgbwOptions[4]{2, 2, 2, 2};
  uint8_t fanSpeed = 0;
  uint8_t fanFlags = 0; // enabled, online, on, pending, intake, direction valid
  uint8_t fanPreset = 50;
  uint8_t fanSource = 159;
  uint8_t fanInstance = 1;
  uint8_t fanError = 0; // 1 bus fault, 2 address conflict, 3 command not confirmed

  bool usbEnabled = false;
  bool waterPumpEnabled = false;
  bool accessory3Enabled = false;
  bool accessory4Enabled = false;
};

struct SystemStatus {
  BatteryStatus battery;
  SensorStatus sensors;
  OutputStatus outputs;
  bool accessory3Active = false;
  bool accessory4Active = false;
  bool bleClientConnected = false;
  uint8_t sp630eAssigned = 0;
  uint8_t sp630eAvailable = 0;
  uint32_t uptimeSeconds = 0;
};
