#include <Arduino.h>
#include <Wire.h>

#include "AppConfig.h"
#include "BatteryManager.h"
#include "BleManager.h"
#include "CerboWifiManager.h"
#if BLUESQUID_TOUCH_RS485_BENCH
#include "BenchRs485Manager.h"
#else
#include "RvcFanManager.h"
#endif
#include "EventManager.h"
#include "Logging.h"
#include "LightSwitchManager.h"
#include "OutputController.h"
#include "PwmManager.h"
#include "RgbwBleDriverManager.h"
#include "Sp630eBleAdapter.h"
#include "SensorManager.h"
#include "SettingsManager.h"
#include "SystemTypes.h"

namespace {
constexpr char kTag[] = "Main";

EventManager eventManager;
SettingsManager settingsManager;
CerboWifiManager cerboWifiManager(settingsManager);
PwmManager pwmManager(AppConfig::I2c::kPca9685Address);
OutputController outputController(pwmManager, eventManager, settingsManager);
RgbwBleDriverManager rgbwBleDriverManager(outputController);
Sp630eBleAdapter sp630eControllers[5] = {
    {"SP630E controller 1"}, {"SP630E controller 2"},
    {"SP630E controller 3"}, {"SP630E controller 4"},
    {"SP630E controller 5"}};
Sp630eAssignment sp630eAssignments[kSp630eAssignmentCount]{};
LightSwitchManager lightSwitchManager(outputController);
SensorManager sensorManager(eventManager, settingsManager);
BatteryManager batteryManager(eventManager, settingsManager);
BleManager bleManager(eventManager, outputController, sensorManager,
                      batteryManager, settingsManager, cerboWifiManager);
#if BLUESQUID_TOUCH_RS485_BENCH
BenchRs485Manager transportManager(outputController, batteryManager,
                                   sensorManager);
#else
RvcFanManager rvcFanManager(outputController, settingsManager);
#endif

uint32_t lastStatusPublishMs = 0;
#if BLUESQUID_TOUCH_RS485_BENCH
uint32_t lastRs485DiagnosticMs = 0;
#endif
bool appWasConnected = false;

void configureSp630eAssignments() {
  settingsManager.loadSp630eAssignments(sp630eAssignments);
  rgbwBleDriverManager.clearAssignments();
  String addresses[5];
  uint8_t controllerCount = 0;
  for (uint8_t target = 0; target < kSp630eAssignmentCount; ++target) {
    const char* address = sp630eAssignments[target].address;
    if (address[0] == '\0') continue;
    uint8_t controller = controllerCount;
    for (uint8_t index = 0; index < controllerCount; ++index) {
      if (addresses[index].equalsIgnoreCase(address)) {
        controller = index;
        break;
      }
    }
    if (controller == controllerCount) {
      if (controllerCount >= 5) continue;
      addresses[controller] = address;
      sp630eControllers[controller].setAddress(address);
      ++controllerCount;
    }
    if (target < 4) {
      rgbwBleDriverManager.setAdapter(target, &sp630eControllers[controller], sp630eAssignments[target].channel);
    } else if (sp630eAssignments[target].channel <= 4) {
      rgbwBleDriverManager.setAccessoryAdapter(target - 4, &sp630eControllers[controller], sp630eAssignments[target].channel);
    }
  }
}

SystemStatus collectSystemStatus() {
  SystemStatus status;
  status.battery = batteryManager.status();
  status.sensors = sensorManager.status();
  status.outputs = outputController.status();
  rgbwBleDriverManager.availability(status.sp630eAssigned, status.sp630eAvailable);
  status.bleClientConnected = bleManager.isClientConnected();
  status.uptimeSeconds = millis() / 1000U;
  // TODO: Read accessory feedback independently from its requested state.
  status.accessory3Active = status.outputs.accessory3Enabled;
  status.accessory4Active = status.outputs.accessory4Enabled;
  return status;
}

void publishPeriodicStatus() {
  const uint32_t now = millis();
  if (now - lastStatusPublishMs < AppConfig::kStatusPublishIntervalMs) {
    return;
  }
  lastStatusPublishMs = now;

  const SystemStatus status = collectSystemStatus();
  bleManager.publishStatus(status);
#if BLUESQUID_TOUCH_RS485_BENCH
  transportManager.publishStatus(status);
#endif
}
}  // namespace

void setup() {
  Serial.begin(AppConfig::kSerialBaud);
  delay(250);
  LOG_INFO(kTag, "%s firmware %s starting",
           AppConfig::kProductName, AppConfig::kFirmwareVersion);

  Wire.begin(AppConfig::I2c::kSdaPin, AppConfig::I2c::kSclPin,
             AppConfig::I2c::kFrequencyHz);

  settingsManager.begin();
  cerboWifiManager.begin();
  pwmManager.begin();
  outputController.begin();
  lightSwitchManager.begin();
  sensorManager.begin();
  batteryManager.begin();
  bleManager.begin();
  configureSp630eAssignments();
  rgbwBleDriverManager.begin();
#if BLUESQUID_TOUCH_RS485_BENCH
  transportManager.begin();
#else
  rvcFanManager.begin();
#endif
  batteryManager.startBackgroundTask();

  LOG_INFO(kTag, "System initialization complete");
}

void loop() {
  outputController.update();
  rgbwBleDriverManager.update();
  sensorManager.update();
  bleManager.update();
  cerboWifiManager.update();
#if BLUESQUID_TOUCH_RS485_BENCH
  transportManager.update();
#else
  rvcFanManager.update();
#endif
#if BLUESQUID_TOUCH_RS485_BENCH
  if (millis() - lastRs485DiagnosticMs >= 2000) {
    lastRs485DiagnosticMs = millis();
    LOG_INFO(kTag, "RS485 diagnostic: bytes=%lu valid_frames=%lu",
             static_cast<unsigned long>(transportManager.receivedByteCount()),
             static_cast<unsigned long>(transportManager.validFrameCount()));
  }
#endif
  if (lightSwitchManager.update()) {
    bleManager.publishStatus(collectSystemStatus());
#if BLUESQUID_TOUCH_RS485_BENCH
    transportManager.publishStatus(collectSystemStatus());
#endif
    lastStatusPublishMs = millis();
  }

  const bool appConnected = bleManager.isClientConnected();
  if (appConnected && !appWasConnected) {
    bleManager.publishStatus(collectSystemStatus());
#if BLUESQUID_TOUCH_RS485_BENCH
    transportManager.publishStatus(collectSystemStatus());
#endif
    lastStatusPublishMs = millis();
  }
  appWasConnected = appConnected;

  if (batteryManager.consumeStatusChanged()) {
    // Battery energy totals can change on every background sample. Let the
    // rate-limited publisher below send the latest snapshot instead of
    // flooding the half-duplex RS-485 bus with complete status bursts.
    publishPeriodicStatus();
  }

  publishPeriodicStatus();

  delay(AppConfig::kMainLoopDelayMs);
}
