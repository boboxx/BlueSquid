#pragma once

#include <Arduino.h>
#include <atomic>
#include <NimBLEAddress.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "BlueSquidControlProtocol.h"
#include "BleReconnectWatchdog.h"
#include "TouchRemoteStatus.h"

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;
class TouchBleAdvertisedCallbacks;
class TouchBleClientCallbacks;

class TouchBleClient {
 public:
  bool begin();
  void update();
  bool send(BlueSquidControl::Command command, uint8_t target, uint16_t value);
  const TouchRemoteStatus& status() const { return status_; }
  bool connected() const;
  void setDisplaySleeping(bool sleeping);
  uint32_t receivedSnapshotCount() const { return receivedSnapshotCount_; }
  uint32_t receivedAckCount() const { return receivedAckCount_; }
  bool requestSp630eDiscovery();
  bool requestSp630eConfiguration();
  bool saveSp630eConfiguration(const String& body);
  bool assignSp630e(uint8_t target, uint8_t channel,
                     const String& address);
  bool requestCerboWifiConfiguration();
  bool requestRvcFanConfiguration();
  bool saveRvcFanConfiguration(uint8_t enabled, uint8_t instance, uint8_t source);
  bool configureCerboWifi(const String& ssid, const String& password,
                          uint8_t vebusUnitId);
  const String& sp630ePayload() const { return sp630ePayload_; }
  uint32_t sp630ePayloadRevision() const { return sp630ePayloadRevision_; }

 private:
  friend class TouchBleAdvertisedCallbacks;
  friend class TouchBleClientCallbacks;

  void foundRear(NimBLEAdvertisedDevice* device);
  bool connectToRear();
  static void connectionTaskEntry(void* context);
  void disconnected();
  void processSnapshot(const uint8_t* data, size_t length);
  void processAck(const uint8_t* data, size_t length);
  void startScan();
  bool queueConfiguration(const String& command, bool config);
  void applyConnectionParameters();
  bool sendValueCommand(BlueSquidControl::Command command, uint8_t target,
                        uint16_t value);
  bool sendRgbwState(uint8_t zone);

  static void snapshotNotification(NimBLERemoteCharacteristic*, uint8_t* data,
                                   size_t length, bool);
  static void ackNotification(NimBLERemoteCharacteristic*, uint8_t* data,
                              size_t length, bool);
  static void discoveryNotification(NimBLERemoteCharacteristic*, uint8_t* data,
                                    size_t length, bool);

  TouchRemoteStatus status_{};
  NimBLEAddress rearAddress_;
  std::atomic_bool rearAddressValid_{false};
  portMUX_TYPE scanMutex_ = portMUX_INITIALIZER_UNLOCKED;
  struct ConfigurationRequest {
    bool config;
    char command[320];
  };
  QueueHandle_t configurationQueue_ = nullptr;
  NimBLEClient* client_ = nullptr;
  NimBLERemoteCharacteristic* snapshotCharacteristic_ = nullptr;
  NimBLERemoteCharacteristic* commandCharacteristic_ = nullptr;
  NimBLERemoteCharacteristic* ackCharacteristic_ = nullptr;
  NimBLERemoteCharacteristic* configCharacteristic_ = nullptr;
  NimBLERemoteCharacteristic* discoveryCharacteristic_ = nullptr;
  String sp630ePayload_;
  uint32_t sp630ePayloadRevision_ = 0;
  uint16_t commandSequence_ = 1;
  std::atomic<uint32_t> lastConnectAttemptMs_{0};
  uint32_t receivedSnapshotCount_ = 0;
  uint32_t receivedAckCount_ = 0;
  uint32_t lastAckRevision_ = 0;
  std::atomic_bool connectRequested_{false};
  SemaphoreHandle_t connectionWake_ = nullptr;
  TaskHandle_t connectionTask_ = nullptr;
  std::atomic_bool connectionInProgress_{false};
  BleReconnectWatchdog reconnectWatchdog_;
  uint32_t lastHealthLogMs_ = 0;
  uint32_t lastScanAttemptMs_ = 0;
  std::atomic<const char*> connectionStage_{"idle"};
  bool initialized_ = false;
  bool displaySleeping_ = false;
  bool assignmentRefreshPending_ = true;
  bool rgbwPending_[4]{};
  bool rgbwAwaitingAck_[4]{};
  uint16_t rgbwSequence_[4]{};
  uint32_t rgbwChangedMs_[4]{};
  uint32_t rgbwAckStartedMs_[4]{};
};
