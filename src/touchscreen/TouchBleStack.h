#pragma once

// The 7-inch ESP32-S3 has an on-chip BLE controller and uses NimBLE-Arduino.
// The ESP32-P4 has no radio: the core BLE library reaches the ESP32-C6
// controller through ESP-Hosted. Both expose the NimBLE-style client API.
#if defined(BLUESQUID_TOUCHSCREEN_10IN) && BLUESQUID_TOUCHSCREEN_10IN
#define BLUESQUID_TOUCH_CORE_BLE 1
#include <BLEAddress.h>
class BLEAdvertisedDevice;
class BLEClient;
class BLERemoteCharacteristic;
using TouchBleAddress = BLEAddress;
using TouchBleClientHandle = BLEClient;
using TouchBleCharacteristic = BLERemoteCharacteristic;
#else
#define BLUESQUID_TOUCH_CORE_BLE 0
#include <NimBLEAddress.h>
class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;
using TouchBleAddress = NimBLEAddress;
using TouchBleClientHandle = NimBLEClient;
using TouchBleCharacteristic = NimBLERemoteCharacteristic;
#endif
