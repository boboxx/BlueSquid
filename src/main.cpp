#include <Arduino.h>

#include "AppConfig.h"
#include "FirmwareUpdate.h"
#include "BatteryManager.h"
#include "BleManager.h"
#include "CerboWifiManager.h"
#include "RvcFanManager.h"
#include "EventManager.h"
#include "Logging.h"
#include "LightSwitchManager.h"
#include "OutputController.h"
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
OutputController outputController(eventManager, settingsManager);
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
RvcFanManager rvcFanManager(outputController, settingsManager);

uint32_t lastStatusPublishMs = 0;
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
}
}  // namespace

void setup() {
  Serial.begin(AppConfig::kSerialBaud);
  delay(250);
  LOG_INFO(kTag, "%s firmware %s starting",
           AppConfig::kProductName, AppConfig::kFirmwareVersion);

  settingsManager.begin();
  cerboWifiManager.begin();
  outputController.begin();
  lightSwitchManager.begin();
  sensorManager.begin();
  batteryManager.begin();
  if (!FirmwareUpdate::begin()) LOG_WARN(kTag, "Firmware update service unavailable");
  bleManager.begin();
  configureSp630eAssignments();
  rgbwBleDriverManager.begin();
  rvcFanManager.begin();
  batteryManager.startBackgroundTask();

  LOG_INFO(kTag, "System initialization complete");
}

void loop() {
  outputController.update();
  const bool lightFeedbackChanged = rgbwBleDriverManager.update();
  sensorManager.update();
  bleManager.update();
  cerboWifiManager.update();
  rvcFanManager.update();
  const bool lightSwitchChanged = lightSwitchManager.update();
  if (lightSwitchManager.consumePairingRequest()) bleManager.openPairingWindow();
  if (lightSwitchChanged || lightFeedbackChanged) {
    bleManager.publishStatus(collectSystemStatus());
    lastStatusPublishMs = millis();
  }

  const bool appConnected = bleManager.isClientConnected();
  if (appConnected && !appWasConnected) {
    bleManager.publishStatus(collectSystemStatus());
    lastStatusPublishMs = millis();
  }
  appWasConnected = appConnected;

  if (batteryManager.consumeStatusChanged()) {
    // Battery energy totals can change on every background sample. Let the
    // rate-limited publisher below send the latest snapshot instead of
    // flooding BLE with complete status bursts.
    publishPeriodicStatus();
  }

  publishPeriodicStatus();

  delay(AppConfig::kMainLoopDelayMs);
}
