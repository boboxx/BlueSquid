#pragma once

#include <Arduino.h>

#include "SystemTypes.h"

#ifndef BLUESQUID_SIMULATED_HARDWARE
#define BLUESQUID_SIMULATED_HARDWARE 0
#endif

namespace AppConfig {

constexpr char kProductName[] = "BlueSquid Camper Control";
// Independent device releases. The build selects the version for this target.
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
constexpr char kFirmwareVersion[] = "1.0.23";
constexpr uint8_t kFirmwareVersionMajor = 1;
constexpr uint8_t kFirmwareVersionMinor = 0;
constexpr uint8_t kFirmwareVersionPatch = 23;
#else
constexpr char kFirmwareVersion[] = "1.0.16";
constexpr uint8_t kFirmwareVersionMajor = 1;
constexpr uint8_t kFirmwareVersionMinor = 0;
constexpr uint8_t kFirmwareVersionPatch = 16;
#endif
constexpr char kBleDeviceName[] = "BlueSquid";

constexpr uint32_t kSerialBaud = 115200;
constexpr uint32_t kMainLoopDelayMs = 5;
constexpr uint32_t kStatusPublishIntervalMs = 1000;
constexpr uint32_t kDeviceStateSaveDelayMs = 3000;
constexpr uint32_t kRgbwCommandSettleMs = 100;

namespace Ble {
constexpr char kServiceUuid[] = "7D8B1000-8A75-4E41-9A6A-35D0A7A18B01";
constexpr char kStatusUuid[] = "7D8B1001-8A75-4E41-9A6A-35D0A7A18B01";
constexpr char kCommandUuid[] = "7D8B1002-8A75-4E41-9A6A-35D0A7A18B01";
constexpr char kConfigUuid[] = "7D8B1003-8A75-4E41-9A6A-35D0A7A18B01";
constexpr char kBmsDiscoveryUuid[] = "7D8B1004-8A75-4E41-9A6A-35D0A7A18B01";
constexpr uint8_t kProtocolVersion = 2;
constexpr size_t kStatusPacketSize = 64;
constexpr uint32_t kSecondaryClientIdleTimeoutMs = 5UL * 60UL * 1000UL;
}  // namespace Ble

namespace Battery {
#ifndef BLUESQUID_SIMULATED_BATTERY
#define BLUESQUID_SIMULATED_BATTERY BLUESQUID_SIMULATED_HARDWARE
#endif
constexpr bool kSimulatedBattery = BLUESQUID_SIMULATED_BATTERY != 0;
#ifndef BLUESQUID_BATTERY_CAPACITY_AH
#define BLUESQUID_BATTERY_CAPACITY_AH 200
#endif
constexpr float kCapacityAh = BLUESQUID_BATTERY_CAPACITY_AH;
}  // namespace Battery

namespace I2c {
// HTU21D climate sensor remains on its dedicated I2C bus.
#ifndef BLUESQUID_CLIMATE_I2C_SDA_PIN
#define BLUESQUID_CLIMATE_I2C_SDA_PIN 18
#endif
#ifndef BLUESQUID_CLIMATE_I2C_SCL_PIN
#define BLUESQUID_CLIMATE_I2C_SCL_PIN 19
#endif
constexpr int kClimateSdaPin = BLUESQUID_CLIMATE_I2C_SDA_PIN;
constexpr int kClimateSclPin = BLUESQUID_CLIMATE_I2C_SCL_PIN;
constexpr uint32_t kClimateFrequencyHz = 100000;
}  // namespace I2c

namespace Sensors {
constexpr uint32_t kAttitudeSampleIntervalMs = 100;
constexpr uint32_t kClimateSampleIntervalMs = 2000;
constexpr uint32_t kAttitudeStartupSettleMs = 2000;
constexpr uint8_t kAdcSamplesPerReading = 16;
constexpr uint8_t kCalibrationSampleCount = 32;
constexpr uint32_t kCalibrationSampleDelayMs = 10;
constexpr uint32_t kAttitudeLogIntervalMs = 5000;
#ifndef BLUESQUID_GY61_X_PIN
#define BLUESQUID_GY61_X_PIN 32
#endif
#ifndef BLUESQUID_GY61_Y_PIN
#define BLUESQUID_GY61_Y_PIN 33
#endif
#ifndef BLUESQUID_GY61_Z_PIN
#define BLUESQUID_GY61_Z_PIN 34
#endif
constexpr int kGy61XPin = BLUESQUID_GY61_X_PIN;
constexpr int kGy61YPin = BLUESQUID_GY61_Y_PIN;
constexpr int kGy61ZPin = BLUESQUID_GY61_Z_PIN;

// Initial ADXL335/GY-61 values at 3.3 V. Fine-tune these after mounting using
// the raw millivolt values printed in the serial console.
constexpr float kGy61XZeroMv = 1650.0F;
constexpr float kGy61YZeroMv = 1650.0F;
constexpr float kGy61ZZeroMv = 1650.0F;
constexpr float kGy61SensitivityMvPerG = 330.0F;
constexpr float kAttitudeFilterAlpha = 0.30F;
constexpr bool kInvertPitch = false;
constexpr bool kInvertRoll = false;
}  // namespace Sensors

namespace Outputs {
// TODO: Assign protected GPIOs after the relay/MOSFET driver design is final.
constexpr int kUsbEnablePin = -1;
constexpr int kWaterPumpPin = -1;

// Generic third accessory output. The final PCB must use a protected driver
// appropriate to the assigned load; never drive a field load directly.
#ifndef BLUESQUID_ACCESSORY3_PIN
#define BLUESQUID_ACCESSORY3_PIN -1
#endif
#ifndef BLUESQUID_ACCESSORY3_ACTIVE_HIGH
#define BLUESQUID_ACCESSORY3_ACTIVE_HIGH 1
#endif
constexpr int kAccessory3Pin = BLUESQUID_ACCESSORY3_PIN;
constexpr bool kAccessory3ActiveHigh = BLUESQUID_ACCESSORY3_ACTIVE_HIGH != 0;

#ifndef BLUESQUID_ACCESSORY4_PIN
#define BLUESQUID_ACCESSORY4_PIN -1
#endif
#ifndef BLUESQUID_ACCESSORY4_ACTIVE_HIGH
#define BLUESQUID_ACCESSORY4_ACTIVE_HIGH 1
#endif
constexpr int kAccessory4Pin = BLUESQUID_ACCESSORY4_PIN;
constexpr bool kAccessory4ActiveHigh = BLUESQUID_ACCESSORY4_ACTIVE_HIGH != 0;

// Eight independent low-side PWM outputs for two common-anode RGBW strips.
// Each GPIO must drive its own transistor/MOSFET channel through a resistor.
#ifndef BLUESQUID_RGBW_RED_PIN
#define BLUESQUID_RGBW_RED_PIN 25
#endif
#ifndef BLUESQUID_RGBW_GREEN_PIN
#define BLUESQUID_RGBW_GREEN_PIN 26
#endif
#ifndef BLUESQUID_RGBW_BLUE_PIN
#define BLUESQUID_RGBW_BLUE_PIN 27
#endif
#ifndef BLUESQUID_RGBW_WHITE_PIN
#define BLUESQUID_RGBW_WHITE_PIN 13
#endif
#ifndef BLUESQUID_BED_RGBW_RED_PIN
#define BLUESQUID_BED_RGBW_RED_PIN 4
#endif
#ifndef BLUESQUID_BED_RGBW_GREEN_PIN
#define BLUESQUID_BED_RGBW_GREEN_PIN 14
#endif
#ifndef BLUESQUID_BED_RGBW_BLUE_PIN
#define BLUESQUID_BED_RGBW_BLUE_PIN 16
#endif
#ifndef BLUESQUID_BED_RGBW_WHITE_PIN
#define BLUESQUID_BED_RGBW_WHITE_PIN 17
#endif
constexpr int kFrontRgbwPins[4] = {
    BLUESQUID_RGBW_RED_PIN, BLUESQUID_RGBW_GREEN_PIN,
    BLUESQUID_RGBW_BLUE_PIN, BLUESQUID_RGBW_WHITE_PIN};
constexpr int kBedRgbwPins[4] = {
    BLUESQUID_BED_RGBW_RED_PIN, BLUESQUID_BED_RGBW_GREEN_PIN,
    BLUESQUID_BED_RGBW_BLUE_PIN, BLUESQUID_BED_RGBW_WHITE_PIN};
constexpr bool kRgbwActiveHigh = true;
constexpr uint32_t kRgbwPwmFrequencyHz = 1000;

#ifndef BLUESQUID_ALL_LIGHTS_SWITCH_PIN
#define BLUESQUID_ALL_LIGHTS_SWITCH_PIN 23
#endif
constexpr int kAllLightsSwitchPin = BLUESQUID_ALL_LIGHTS_SWITCH_PIN;
constexpr bool kAllLightsSwitchActiveLow = true;
constexpr uint32_t kAllLightsSwitchDebounceMs = 50;
}  // namespace Outputs

namespace Can {
#ifndef BLUESQUID_CAN_TX_PIN
#define BLUESQUID_CAN_TX_PIN 37
#endif
#ifndef BLUESQUID_CAN_RX_PIN
#define BLUESQUID_CAN_RX_PIN 38
#endif
constexpr int kTxPin = BLUESQUID_CAN_TX_PIN;
constexpr int kRxPin = BLUESQUID_CAN_RX_PIN;
}  // namespace Can

}  // namespace AppConfig
