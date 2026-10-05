#pragma once

#include <Arduino.h>
#include <atomic>
#include <NimBLEAddress.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "BlueSquidControlProtocol.h"
#include "BleReconnectWatchdog.h"
#include "BleDiscoveryRecovery.h"
#include "TouchRemoteStatus.h"
#include "OtaCredentials.h"
#include "OtaLink.h"

class NimBLEAdvertisedDevice;
class NimBLEClient;
class NimBLERemoteCharacteristic;
class TouchBleAdvertisedCallbacks;
class TouchBleClientCallbacks;

class TouchBleClient {
 public:
  bool requestControllerUpdate(bool start);
  OtaLink::View controllerUpdateStatus() const;
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
  void updateConnection();
  void maintainConnection();
  std::atomic_bool maintenanceBusy_{false};
  void synchronizeOtaCredentials();
  void updateControllerNetwork();
  std::atomic<NimBLERemoteCharacteristic*> otaLinkCharacteristic_{nullptr};
  std::atomic<uint8_t> otaLinkAction_{0};
  bool otaLinkWanted_ = false;
  uint32_t otaLinkId_ = 0, otaLinkPollMs_ = 0, otaLinkWriteMs_ = 0;
  OtaLink::View otaLinkView_;
  mutable portMUX_TYPE otaLinkMux_ = portMUX_INITIALIZER_UNLOCKED;
  std::atomic<NimBLERemoteCharacteristic*> otaCredentialsCharacteristic_{nullptr};
  std::atomic<uint32_t> otaConnectionRevision_{0};
  uint32_t otaObservedRevision_ = 0;
  uint32_t otaLastPollMs_ = 0, otaLastWriteMs_ = 0;
  OtaCredentials::SyncState otaCredentialsSync_;
  friend class TouchBleAdvertisedCallbacks;
  friend class TouchBleClientCallbacks;

  void foundRear(NimBLEAdvertisedDevice* device);
  bool connectToRear();
  static void connectionTaskEntry(void* context);
  void disconnected();
  void processSnapshot(const uint8_t* data, size_t length);
  void processAck(const uint8_t* data, size_t length);
  void startScan();
  void setConnectionStage(const char* stage);
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
  NimBLEAddress lastConnectedAddress_;
  std::atomic_bool lastConnectedAddressValid_{false};
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
  BleDiscoveryRecovery discoveryRecovery_;
  bool stableOnline_ = false;
  uint32_t stableOnlineSince_ = 0;
  std::atomic<uint32_t> connectionProgress_{0};
  uint32_t lastHealthLogMs_ = 0;
  uint32_t lastScanAttemptMs_ = 0;
  std::atomic<const char*> connectionStage_{"idle"};
  bool initialized_ = false;
  std::atomic_bool displaySleeping_{false};
  bool assignmentRefreshPending_ = true;
  bool rgbwPending_[4]{};
  bool rgbwAwaitingAck_[4]{};
  uint16_t rgbwSequence_[4]{};
  uint32_t rgbwChangedMs_[4]{};
  uint32_t rgbwAckStartedMs_[4]{};
};
