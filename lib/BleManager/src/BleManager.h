#pragma once

#include <Arduino.h>

#include <string>

#include "BatteryManager.h"
#include "CerboWifiManager.h"
#include "EventManager.h"
#include "OutputController.h"
#include "SensorManager.h"
#include "SettingsManager.h"
#include "SystemTypes.h"

class NimBLECharacteristic;
class NimBLEServer;
class BleConfigCallbacks;
class BleCommandCallbacks;
class BleDiscoveryCallbacks;
class BleServerCallbacks;
class BleStatusCallbacks;
class BleTouchCommandCallbacks;
class BleTouchSnapshotCallbacks;

class BleManager {
 public:
  BleManager(EventManager& eventManager, OutputController& outputController,
             SensorManager& sensorManager, BatteryManager& batteryManager,
             SettingsManager& settingsManager, CerboWifiManager& cerboWifi);

  bool begin();
  void update();
  void publishStatus(const SystemStatus& status);
  bool isClientConnected() const;
  uint8_t connectedClientCount() const;

 private:
  struct PendingCommand {
    uint16_t connectionHandle = 0xFFFF;
    uint8_t opcode = 0;
    uint8_t value = 0;
  };

  struct ConnectedClient {
    uint16_t connectionHandle = 0xFFFF;
    uint32_t lastActivityMs = 0;
    bool primary = false;
    bool connected = false;
  };

  static constexpr uint8_t kCommandQueueCapacity = 16;
  static constexpr uint8_t kMaximumClients = CONFIG_BT_NIMBLE_MAX_CONNECTIONS;

  friend class BleCommandCallbacks;
  friend class BleConfigCallbacks;
  friend class BleDiscoveryCallbacks;
  friend class BleServerCallbacks;
  friend class BleStatusCallbacks;
  friend class BleTouchCommandCallbacks;
  friend class BleTouchSnapshotCallbacks;

  void setBatteryCapacityConfig(const std::string& value);
  void requestSp630eDiscovery();
  void setSp630eConfig(const std::string& value);
  String sp630eConfig() const;
  void setCerboWifiConfig(const std::string& value);
  String cerboWifiConfig() const;
  String rvcFanConfig() const;
  void setRvcFanConfig(const std::string& value);
  void queueCommand(uint16_t connectionHandle, uint8_t opcode, uint8_t value);
  bool dequeueCommand(PendingCommand& command);
  void processCommand(uint16_t connectionHandle, uint8_t opcode, uint8_t value);
  void publishLatestStatus();
  void publishTouchSnapshot(const SystemStatus& status, bool notify);
  void queueTouchCommand(uint16_t connectionHandle, const uint8_t* data,
                         size_t length);
  void processTouchCommand();
  void acknowledgeTouchCommand(uint16_t connectionHandle, uint16_t sequence,
                               uint8_t command, uint8_t target,
                               uint8_t result);
  void registerClient(uint16_t connectionHandle);
  void unregisterClient(uint16_t connectionHandle);
  void recordClientActivity(uint16_t connectionHandle);
  void setClientPrimary(uint16_t connectionHandle, bool primary);
  void disconnectIdleClients();
  void maintainAdvertising();
  void setConnectedClientCount(uint8_t count);
  static void encodeStatus(const SystemStatus& status, uint8_t* packet);

  EventManager& eventManager_;
  OutputController& outputController_;
  SensorManager& sensorManager_;
  BatteryManager& batteryManager_;
  SettingsManager& settingsManager_;
  CerboWifiManager& cerboWifi_;
  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* statusCharacteristic_ = nullptr;
  NimBLECharacteristic* configCharacteristic_ = nullptr;
  NimBLECharacteristic* discoveryCharacteristic_ = nullptr;
  NimBLECharacteristic* touchSnapshotCharacteristic_ = nullptr;
  NimBLECharacteristic* touchAckCharacteristic_ = nullptr;
  SystemStatus latestStatus_ = {};
  String latestDiscoveryPayload_;
  bool hasLatestStatus_ = false;
  bool sp630eDiscoveryRequested_ = false;
  bool sp630eDiscoveryRunning_ = false;
  bool restartRequested_ = false;
  uint32_t sp630eDiscoveryStartedMs_ = 0;
  uint32_t lastAdvertisingCheckMs_ = 0;
  uint32_t lastAdvertisingDiagnosticMs_ = 0;
  uint32_t touchStateRevision_ = 0;
  struct PendingTouchCommand {
    uint16_t connectionHandle = 0xFFFF;
    uint16_t sequence = 0;
    uint8_t command = 0;
    uint8_t target = 0;
    uint8_t length = 0;
    uint8_t payload[12]{};
  } touchCommandQueue_[kCommandQueueCapacity];
  uint8_t touchCommandQueueHead_ = 0;
  uint8_t touchCommandQueueTail_ = 0;
  uint8_t touchCommandQueueCount_ = 0;
  volatile uint8_t connectedClientCount_ = 0;
  ConnectedClient clients_[kMaximumClients] = {};
  PendingCommand commandQueue_[kCommandQueueCapacity] = {};
  uint8_t commandQueueHead_ = 0;
  uint8_t commandQueueTail_ = 0;
  uint8_t commandQueueCount_ = 0;
  portMUX_TYPE commandQueueMux_ = portMUX_INITIALIZER_UNLOCKED;
  portMUX_TYPE clientMux_ = portMUX_INITIALIZER_UNLOCKED;
};
