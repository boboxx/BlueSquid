#include "TouchBleClient.h"
#include "TouchHotspot.h"

#include <NimBLEDevice.h>
#include <NimBLEUtils.h>

#include "BlueSquidBleProtocol.h"
#include "AppConfig.h"

namespace {
TouchBleClient* activeClient = nullptr;
constexpr char kTouchDeviceName[] = "BlueSquid-Touch";
// Survives a software reset; rearmed only after a minute of healthy operation.
RTC_DATA_ATTR bool discoveryRestartUsed = false;
}

class TouchBleAdvertisedCallbacks final
    : public NimBLEAdvertisedDeviceCallbacks {
 public:
  explicit TouchBleAdvertisedCallbacks(TouchBleClient& owner)
      : owner_(owner) {}

  void onResult(NimBLEAdvertisedDevice* device) override {
    const bool serviceMatch = device->isAdvertisingService(
        NimBLEUUID(BlueSquidBle::kServiceUuid)) ||
        device->isAdvertisingService(NimBLEUUID(AppConfig::Ble::kServiceUuid));
    const bool nameMatch = device->haveName() &&
        device->getName() == AppConfig::kBleDeviceName;
    if (serviceMatch || nameMatch) {
      owner_.foundRear(device);
    }
  }

 private:
  TouchBleClient& owner_;
};

class TouchBleClientCallbacks final : public NimBLEClientCallbacks {
 public:
  explicit TouchBleClientCallbacks(TouchBleClient& owner) : owner_(owner) {}

  void onDisconnect(NimBLEClient*) override { owner_.disconnected(); }

  bool onConnParamsUpdateRequest(NimBLEClient*,
                                 const ble_gap_upd_params*) override {
    return true;
  }

  void onAuthenticationComplete(ble_gap_conn_desc* description) override {
    if (!description->sec_state.encrypted) {
      NimBLEClient* client =
          NimBLEDevice::getClientByID(description->conn_handle);
      if (client != nullptr) client->disconnect();
    }
  }

 private:
  TouchBleClient& owner_;
};

bool TouchBleClient::begin() {
  activeClient = this;
  // The Controller sends acknowledgements to every connected touchscreen.
  // Random starting points keep two touchscreens from sharing sequences.
  commandSequence_ = static_cast<uint16_t>(esp_random() | 1U);
  NimBLEDevice::init(kTouchDeviceName);
  // Observe the actual GAP reason; getLastError() is not the disconnect reason.
  NimBLEDevice::setCustomGapHandler([](ble_gap_event* event, void*) -> int {
    if (event->type == BLE_GAP_EVENT_DISCONNECT) {
      const auto& lost = event->disconnect;
      Serial.printf("BLE Controller link lost: reason=%d (%s), interval=%.2f ms latency=%u timeout=%u ms\n",
          lost.reason, NimBLEUtils::returnCodeToString(lost.reason),
          lost.conn.conn_itvl * 1.25, lost.conn.conn_latency,
          lost.conn.supervision_timeout * 10U);
    }
    return 0;
  });
  NimBLEDevice::setMTU(185);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(
      new TouchBleAdvertisedCallbacks(*this), false);
  scan->setActiveScan(true);
  scan->setInterval(45);
  scan->setWindow(30);
  configurationQueue_ = xQueueCreate(4, sizeof(ConfigurationRequest));
  if (!configurationQueue_) return false;
  connectionWake_ = xSemaphoreCreateBinary();
  if (!connectionWake_) {
    vQueueDelete(configurationQueue_);
    configurationQueue_ = nullptr;
    return false;
  }
  if (xTaskCreate(connectionTaskEntry, "touch-connect", 8192, this, 1,
                  &connectionTask_) != pdPASS) {
    vSemaphoreDelete(connectionWake_);
    connectionWake_ = nullptr;
    vQueueDelete(configurationQueue_);
    configurationQueue_ = nullptr;
    return false;
  }
  initialized_ = true;
  startScan();
  return true;
}

void TouchBleClient::startScan() {
  if (!initialized_ || connected() || connectionInProgress_.load()) return;
  if (millis() - lastScanAttemptMs_ < BlueSquidBle::kReconnectDelayMs) return;
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (!scan->isScanning()) {
    portENTER_CRITICAL(&scanMutex_);
    if (connectRequested_ || connectionInProgress_.load()) {
      portEXIT_CRITICAL(&scanMutex_);
      return;
    }
    rearAddressValid_ = false;
    portEXIT_CRITICAL(&scanMutex_);
    lastScanAttemptMs_ = millis();
    const bool started = scan->start(BlueSquidBle::kScanDurationSeconds, nullptr, false);
    Serial.printf("BLE controller scan: %s\n", started ? "started" : "failed");
  }
}

void TouchBleClient::foundRear(NimBLEAdvertisedDevice* device) {
  if (device == nullptr) return;
  portENTER_CRITICAL(&scanMutex_);
  if (connectRequested_ || connectionInProgress_.load()) {
    portEXIT_CRITICAL(&scanMutex_);
    return;
  }
  rearAddress_ = device->getAddress();
  rearAddressValid_ = true;
  connectRequested_ = true;
  portEXIT_CRITICAL(&scanMutex_);
  NimBLEDevice::getScan()->stop();
  Serial.println("BLE controller advertisement found");
}

void TouchBleClient::restartQuietly() {
  // Restarting during an active scan intermittently panicked inside the
  // restart itself. Stop radio activity without waiting on GATT work, which
  // may be the reason for this restart.
  NimBLEDevice::getScan()->stop();
  delay(100);
  ESP.restart();
}

void TouchBleClient::updateConnection() {
  static_assert(MYNEWT_VAL(BLE_HS_FLOW_CTRL_ITVL) == 20,
                "Touchscreen BLE requires prompt receive-credit returns");
  if (!initialized_) return;
  const uint32_t now = millis();
  const bool connecting = connectionInProgress_.load();
  if (now - lastHealthLogMs_ >= 30000) {
    lastHealthLogMs_ = now;
    Serial.printf("BLE health: online=%u link=%u scanning=%u reconnect=%u stage=%s snapshots=%lu age=%lu heap=%lu minHeap=%lu\n",
        connected(), client_ && client_->isConnected(), NimBLEDevice::getScan()->isScanning(),
        connecting, connectionStage_.load(), static_cast<unsigned long>(receivedSnapshotCount_),
        static_cast<unsigned long>(status_.lastHeartbeatMs ? now - status_.lastHeartbeatMs : 0),
        static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()));
  }
  const auto recovery = reconnectWatchdog_.poll(now, connecting, connectionProgress_.load());
  if (recovery == BleReconnectWatchdog::Action::Restart) {
    // Never delete a blocked NimBLE task: it may still own stack-backed GATT state.
    Serial.printf("BLE reconnect cancellation stuck at %s for 30 seconds; restarting touchscreen\n", connectionStage_.load());
    restartQuietly();
    return;
  }
  if (recovery == BleReconnectWatchdog::Action::Disconnect) {
    Serial.printf("BLE reconnect made no progress at %s for 30 seconds; requesting disconnect\n", connectionStage_.load());
    if (client_ && client_->isConnected()) client_->disconnect();
  }
  const bool online = connected();
  if (online) {
    if (!stableOnline_) { stableOnline_ = true; stableOnlineSince_ = now; }
    if (now - stableOnlineSince_ >= 60000) discoveryRestartUsed = false;
  } else stableOnline_ = false;
  const auto discoveryAction = discoveryRecovery_.poll(now, online,
      connecting || connectRequested_.load() || (client_ && client_->isConnected()),
      lastConnectedAddressValid_.load(), !discoveryRestartUsed);
  if (discoveryAction == BleDiscoveryRecovery::Action::Restart) {
    discoveryRestartUsed = true;
    Serial.println("BLE discovery recovery exhausted after 120 seconds; restarting touchscreen once");
    restartQuietly();
    return;
  }
  if (discoveryAction == BleDiscoveryRecovery::Action::RetryAddress) {
    // Stop scan callbacks before clearing their result objects. Reserve the
    // connection job before stopping: scan-complete callbacks may run here.
    portENTER_CRITICAL(&scanMutex_);
    connectionInProgress_.store(true);
    connectRequested_ = false;
    rearAddress_ = lastConnectedAddress_;
    rearAddressValid_ = true;
    portEXIT_CRITICAL(&scanMutex_);
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (!scan->stop()) {
      Serial.println("BLE discovery recovery: scan stop failed; will retry");
      connectionInProgress_.store(false);
      return;
    }
    scan->clearResults();
    scan->clearDuplicateCache();
    while (NimBLEDevice::isIgnored(lastConnectedAddress_))
      NimBLEDevice::removeIgnored(lastConnectedAddress_);
    lastConnectAttemptMs_ = now;
    connectionStage_.store("retrying known address");
    reconnectWatchdog_.start(now, connectionProgress_.load());
    Serial.println("BLE discovery recovery: cleared scan state; retrying last connected Controller");
    xSemaphoreGive(connectionWake_);
    return;
  }
  if (connecting) return;
  if (client_ != nullptr && client_->isConnected() &&
      status_.lastHeartbeatMs != 0 &&
      now - status_.lastHeartbeatMs > BlueSquidBle::kOnlineTimeoutMs) {
    Serial.println("BLE status timed out; requesting reconnect");
    client_->disconnect();
    return;
  }
  bool startConnection = false;
  portENTER_CRITICAL(&scanMutex_);
  if (connectRequested_ && rearAddressValid_ &&
      now - lastConnectAttemptMs_.load() >= BlueSquidBle::kReconnectDelayMs) {
    connectRequested_ = false;
    lastConnectAttemptMs_ = now;
    connectionStage_.store("queued");
    reconnectWatchdog_.start(now, connectionProgress_.load());
    connectionInProgress_.store(true);
    startConnection = true;
  }
  portEXIT_CRITICAL(&scanMutex_);
  if (startConnection) {
    xSemaphoreGive(connectionWake_);
    return;
  }
  if (!connected() && !connectRequested_ &&
      now - lastConnectAttemptMs_ >= BlueSquidBle::kReconnectDelayMs) {
    startScan();
  }
}

void TouchBleClient::update() {
  if (!initialized_) return;
  // Never wait for background GATT work in the display/wake loop.
  bool idle = false;
  if (maintenanceBusy_.compare_exchange_strong(idle, true)) {
    updateConnection();
    maintenanceBusy_.store(false);
  }
  const uint32_t now = millis();
  if (connected()) {
    for (uint8_t zone = 0; zone < 4; ++zone) {
      if (rgbwAwaitingAck_[zone] && now - rgbwAckStartedMs_[zone] >= 1500)
        rgbwAwaitingAck_[zone] = false;
      if (rgbwPending_[zone] &&
          now - rgbwChangedMs_[zone] >=
              BlueSquidBle::kInteractiveCoalesceMs) {
        sendRgbwState(zone);
      }
    }
  }
}

void TouchBleClient::maintainConnection() {
  synchronizeOtaCredentials();
  updateControllerNetwork();
  if (assignmentRefreshPending_ && requestSp630eConfiguration())
    assignmentRefreshPending_ = false;
  // Configuration requests originate in LVGL callbacks. Never wait for a
  // GATT response while holding LVGL's lock: the rear may reboot here.
  ConfigurationRequest request{};
  if (configurationQueue_ &&
      xQueueReceive(configurationQueue_, &request, 0) == pdTRUE) {
    NimBLERemoteCharacteristic* characteristic = request.config
        ? configCharacteristic_ : discoveryCharacteristic_;
    if (!connected() || !characteristic) {
      Serial.println("BLE configuration request dropped: controller offline");
    } else {
      const bool sent = characteristic->writeValue(
          reinterpret_cast<const uint8_t*>(request.command),
          strlen(request.command), true);
      Serial.printf("BLE configuration write: %s\n", sent ? "acknowledged" : "failed");
    }
  }
}

void TouchBleClient::connectionTaskEntry(void* context) {
  auto* owner = static_cast<TouchBleClient*>(context);
  for (;;) {
    // NimBLE owns task notifications while synchronous GATT calls wait.
    // Use a separate semaphore for jobs so BLE completions cannot start one.
    if (xSemaphoreTake(owner->connectionWake_, pdMS_TO_TICKS(20)) != pdTRUE) {
      bool idle = false;
      if (owner->maintenanceBusy_.compare_exchange_strong(idle, true)) {
        // The same reservation guards reconnect scheduling in update(). A
        // reconnect cannot delete cached characteristics during a GATT read.
        if (!owner->connectionInProgress_.load() && owner->connected())
          owner->maintainConnection();
        owner->maintenanceBusy_.store(false);
      }
      continue;
    }
    Serial.println("BLE controller reconnect started");
    const bool success = owner->connectToRear();
    if (!success) {
      if (owner->client_ && owner->client_->isConnected()) owner->client_->disconnect();
      owner->disconnected();
    }
    if (success) owner->discoveryRecovery_.connectionCompleted();
    owner->lastConnectAttemptMs_ = millis();
    Serial.printf("BLE controller reconnect %s\n", success ? "complete" : "failed");
    owner->connectionStage_.store(success ? "online" : "retry");
    owner->connectionInProgress_.store(false);
  }
}

void TouchBleClient::setConnectionStage(const char* stage) {
  connectionStage_.store(stage);
  ++connectionProgress_;
}

bool TouchBleClient::connectToRear() {
  portENTER_CRITICAL(&scanMutex_);
  const bool valid = rearAddressValid_;
  const NimBLEAddress address = rearAddress_;
  portEXIT_CRITICAL(&scanMutex_);
  if (!valid) return false;
  // Own one client/callback pair for the lifetime of the touchscreen. NimBLE
  // setClientCallbacks replaces the pointer without deleting the previous one.
  if (client_ == nullptr) {
    client_ = NimBLEDevice::createClient();
    if (client_ == nullptr) return false;
    client_->setClientCallbacks(new TouchBleClientCallbacks(*this), true);
  }
  // Match the SP630E links on the Controller's shared radio. Keep this
  // interval while awake, asleep and discovering services.
  client_->setConnectionParams(32, 32, 0, 300);
  client_->setConnectTimeout(4);
  setConnectionStage("connecting");
  Serial.println("BLE controller stage: connecting");
  if (!client_->connect(address, true)) {
    Serial.printf("BLE controller connect failed: %d\n", client_->getLastError());
    return false;
  }
  setConnectionStage("securing");
  Serial.println("BLE controller stage: securing");
  if (!client_->secureConnection()) {
    Serial.printf("BLE controller security failed: %d\n", client_->getLastError());
    return false;
  }
  setConnectionStage("discovering services");
  Serial.println("BLE controller stage: discovering services");

  NimBLERemoteService* service =
      client_->getService(BlueSquidBle::kServiceUuid);
  if (service == nullptr) return false;
  setConnectionStage("discovering snapshot");
  NimBLERemoteCharacteristic* snapshot =
      service->getCharacteristic(BlueSquidBle::kSnapshotUuid);
  if (snapshot == nullptr) return false;
  setConnectionStage("discovering command");
  NimBLERemoteCharacteristic* command =
      service->getCharacteristic(BlueSquidBle::kCommandUuid);
  if (command == nullptr) return false;
  setConnectionStage("discovering acknowledgement");
  NimBLERemoteCharacteristic* ack = service->getCharacteristic(BlueSquidBle::kAckUuid);
  if (ack == nullptr) return false;
  setConnectionStage("subscribing");
  Serial.println("BLE controller stage: subscribing");
  if (!snapshot->subscribe(true, snapshotNotification)) return false;
  setConnectionStage("subscribing acknowledgement");
  if (!ack->subscribe(true, ackNotification)) return false;
  setConnectionStage("discovering configuration service");
  NimBLERemoteService* legacyService =
      client_->getService(AppConfig::Ble::kServiceUuid);
  NimBLERemoteCharacteristic* config = nullptr;
  NimBLERemoteCharacteristic* discovery = nullptr;
  NimBLERemoteCharacteristic* otaCredentials = nullptr;
  NimBLERemoteCharacteristic* otaLink = nullptr;
  if (legacyService != nullptr) {
    setConnectionStage("discovering configuration");
    config =
        legacyService->getCharacteristic(AppConfig::Ble::kConfigUuid);
    setConnectionStage("discovering device configuration");
    discovery = legacyService->getCharacteristic(
        AppConfig::Ble::kBmsDiscoveryUuid);
    setConnectionStage("discovering OTA credentials");
    otaCredentials = legacyService->getCharacteristic(OtaCredentials::kUuid);
    setConnectionStage("discovering OTA network link");
    otaLink = legacyService->getCharacteristic(OtaLink::kUuid);
    if (discovery != nullptr) {
      setConnectionStage("subscribing configuration");
      discovery->subscribe(true, discoveryNotification);
    }
  }
  setConnectionStage("reading initial status");
  Serial.println("BLE controller stage: reading initial status");
  const std::string initialStatus = snapshot->readValue();
  if (!client_->isConnected()) return false;
  processSnapshot(reinterpret_cast<const uint8_t*>(initialStatus.data()),
                  initialStatus.size());
  if (!client_->isConnected() || status_.lastHeartbeatMs == 0) return false;
  snapshotCharacteristic_ = snapshot;
  commandCharacteristic_ = command;
  ackCharacteristic_ = ack;
  configCharacteristic_ = config;
  discoveryCharacteristic_ = discovery;
  otaCredentialsCharacteristic_.store(otaCredentials);
  otaLinkCharacteristic_.store(otaLink);
  otaConnectionRevision_.fetch_add(1);
  otaLinkPollMs_ = millis() - 30000;
  portENTER_CRITICAL(&scanMutex_);
  lastConnectedAddress_ = address;
  lastConnectedAddressValid_.store(true);
  portEXIT_CRITICAL(&scanMutex_);
  assignmentRefreshPending_ = true;
  return true;
}

void TouchBleClient::disconnected() {
  Serial.println("BLE controller disconnected; reconnect scheduled");
  snapshotCharacteristic_ = nullptr;
  commandCharacteristic_ = nullptr;
  ackCharacteristic_ = nullptr;
  configCharacteristic_ = nullptr;
  discoveryCharacteristic_ = nullptr;
  otaCredentialsCharacteristic_.store(nullptr);
  otaLinkCharacteristic_.store(nullptr);
  portENTER_CRITICAL(&otaLinkMux_);
  otaLinkView_.connected = otaLinkView_.ready = false;
  portEXIT_CRITICAL(&otaLinkMux_);
  connectRequested_ = false;
  status_.lastHeartbeatMs = 0;
  if (configurationQueue_) xQueueReset(configurationQueue_);
  for (uint8_t zone = 0; zone < 4; ++zone) {
    rgbwPending_[zone] = false;
    rgbwAwaitingAck_[zone] = false;
    rgbwSnapshotGate_[zone] = {};
  }
  lastConnectAttemptMs_ = millis();
}

void TouchBleClient::synchronizeOtaCredentials() {
  auto* characteristic = otaCredentialsCharacteristic_.load();
  if (!connected() || !characteristic) return;
  const uint32_t revision = otaConnectionRevision_.load();
  if (revision != otaObservedRevision_) {
    otaObservedRevision_ = revision;
    otaCredentialsSync_.reset();
    otaLastPollMs_ = millis() - 1000;
  }
  const uint32_t now = millis();
  if (uint32_t(now - otaLastPollMs_) < 1000) return;
  otaLastPollMs_ = now;
  const auto desired = TouchHotspot::credentials();
  if (!OtaCredentials::valid(desired)) return;
  if (otaCredentialsSync_.current(desired)) return;
  if (otaCredentialsSync_.needsRequest(desired)) {
    otaCredentialsSync_.begin(desired, esp_random() | 1U);
    otaLastWriteMs_ = now - 3000;
  } else {
    const std::string ack = characteristic->readValue();
    if (ack.size() == 4 && otaCredentialsSync_.acknowledge(OtaCredentials::readId(
        reinterpret_cast<const uint8_t*>(ack.data())))) {
      Serial.println("Controller OTA login synchronized with System hotspot");
      return;
    }
  }
  if (!connected() || otaCredentialsCharacteristic_.load() != characteristic ||
      uint32_t(now - otaLastWriteMs_) < 3000) return;
  uint8_t bytes[OtaCredentials::kWireSize]{};
  OtaCredentials::encode(bytes, otaCredentialsSync_.requested, otaCredentialsSync_.id);
  otaLastWriteMs_ = now;
  characteristic->writeValue(bytes, sizeof(bytes), true);
}

bool TouchBleClient::requestControllerUpdate(bool start) {
  const auto view = controllerUpdateStatus();
  if (start && (!view.connected || !view.supported)) return false;
  portENTER_CRITICAL(&otaLinkMux_);
  otaLinkView_.ready = false;
  if (start) otaLinkView_.status.phase = OtaLink::Phase::Joining;
  portEXIT_CRITICAL(&otaLinkMux_);
  otaLinkAction_.store(start ? 1 : 2);
  return true;
}
OtaLink::View TouchBleClient::controllerUpdateStatus() const {
  portENTER_CRITICAL(&otaLinkMux_); auto copy = otaLinkView_; portEXIT_CRITICAL(&otaLinkMux_);
  if (otaLinkAction_.load()) copy.ready = false;
  return copy;
}
void TouchBleClient::updateControllerNetwork() {
  const uint8_t action = otaLinkAction_.exchange(0);
  if (action) {
    portENTER_CRITICAL(&otaLinkMux_); otaLinkView_.ready = false; portEXIT_CRITICAL(&otaLinkMux_);
    otaLinkWanted_ = action == 1;
    if (otaLinkWanted_) otaLinkId_ = esp_random() | 1U;
    otaLinkPollMs_ = millis() - 1000;
    otaLinkWriteMs_ = millis() - 3000;
  }
  auto* characteristic = otaLinkCharacteristic_.load();
  const uint32_t now = millis();
  const auto previous = controllerUpdateStatus();
  const bool active = otaLinkWanted_ ||
      previous.status.phase == OtaLink::Phase::Joining ||
      previous.status.phase == OtaLink::Phase::Ready;
  const uint32_t pollInterval = active ? 1000 : 30000;
  if (!action && uint32_t(now - otaLinkPollMs_) < pollInterval) return;
  otaLinkPollMs_ = now;
  OtaLink::View view = controllerUpdateStatus();
  view.connected = connected();
  view.supported = characteristic != nullptr;
  view.ready = false;
  if (view.connected && characteristic) {
    const auto bytes = characteristic->readValue();
    OtaLink::Status status;
    const bool received = OtaLink::decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), status);
    if (received) view.status = status;
    const bool loginSynced = otaCredentialsSync_.current(TouchHotspot::credentials());
    bool send = false;
    if (otaLinkWanted_ && loginSynced) {
      send = !received || status.id != otaLinkId_;
      view.ready = received && status.id == otaLinkId_ && status.phase == OtaLink::Phase::Ready;
      if (send) view.status.phase = OtaLink::Phase::Joining;
    } else if (!otaLinkWanted_ && received) {
      // Also clean up an orphaned session after a touchscreen reboot.
      if (status.phase == OtaLink::Phase::Joining || status.phase == OtaLink::Phase::Ready) {
        otaLinkId_ = status.id; send = true;
      } else otaLinkId_ = 0;
    } else if (otaLinkWanted_) view.status.phase = OtaLink::Phase::Joining;
    if (send && connected() && otaLinkCharacteristic_.load() == characteristic &&
        uint32_t(now - otaLinkWriteMs_) >= 3000) {
      uint8_t request[5]{};
      request[0] = otaLinkWanted_ ? 1 : 0;
      OtaCredentials::writeId(request + 1, otaLinkId_);
      otaLinkWriteMs_ = now;
      characteristic->writeValue(request, sizeof(request), true);
    }
  }
  view.connected = connected();
  view.ready = view.ready && view.connected;
  portENTER_CRITICAL(&otaLinkMux_); otaLinkView_ = view; portEXIT_CRITICAL(&otaLinkMux_);
}

bool TouchBleClient::requestSp630eDiscovery() {
  if (!connected() || discoveryCharacteristic_ == nullptr) return false;
  return queueConfiguration("sp630e", false);
}

bool TouchBleClient::requestSp630eConfiguration() {
  if (!connected() || discoveryCharacteristic_ == nullptr) return false;
  return queueConfiguration("sp630e.config", false);
}

bool TouchBleClient::queueConfiguration(const String& command, bool config) {
  if (!configurationQueue_ || !connected() || command.length() >= sizeof(ConfigurationRequest::command))
    return false;
  ConfigurationRequest request{};
  request.config = config;
  strlcpy(request.command, command.c_str(), sizeof(request.command));
  return xQueueSend(configurationQueue_, &request, 0) == pdTRUE;
}

bool TouchBleClient::saveSp630eConfiguration(const String& body) {
  return connected() && configCharacteristic_ && queueConfiguration("sp630e.save=" + body, true);
}

bool TouchBleClient::assignSp630e(uint8_t target, uint8_t channel,
                                   const String& address) {
  if (!connected() || configCharacteristic_ == nullptr || target >= 8)
    return false;
  String command = "sp630e.assign=" + String(target) + "," +
                   String(channel) + "," +
                   (address.isEmpty() ? String("none") : address);
  return queueConfiguration(command, true);
}

bool TouchBleClient::requestRvcFanConfiguration() {
  return connected() && discoveryCharacteristic_ && queueConfiguration("rvc.config", false);
}
bool TouchBleClient::saveRvcFanConfiguration(uint8_t enabled, uint8_t instance, uint8_t source) {
  return connected() && configCharacteristic_ && queueConfiguration("rvc.save=" + String(enabled) + "|" + String(instance) + "|" + String(source), true);
}
bool TouchBleClient::requestCerboWifiConfiguration() {
  if (!connected() || discoveryCharacteristic_ == nullptr) return false;
  return queueConfiguration("wifi.config", false);
}

bool TouchBleClient::configureCerboWifi(const String& ssid,
                                        const String& password,
                                        uint8_t vebusUnitId) {
  if (!connected() || configCharacteristic_ == nullptr) return false;
  const String command = "wifi.ap=" + ssid + "|" + password + "|" +
                         String(vebusUnitId);
  return queueConfiguration(command, true);
}

void TouchBleClient::discoveryNotification(NimBLERemoteCharacteristic*,
                                            uint8_t* data, size_t length,
                                            bool) {
  if (activeClient == nullptr || data == nullptr) return;
  const size_t count = length < sizeof(activeClient->sp630ePayload_)
      ? length : sizeof(activeClient->sp630ePayload_) - 1;
  portENTER_CRITICAL(&activeClient->payloadMux_);
  memcpy(activeClient->sp630ePayload_, data, count);
  activeClient->sp630ePayload_[count] = '\0';
  portEXIT_CRITICAL(&activeClient->payloadMux_);
  ++activeClient->sp630ePayloadRevision_;
}

String TouchBleClient::sp630ePayload() const {
  char copy[sizeof(sp630ePayload_)];
  portENTER_CRITICAL(&payloadMux_);
  memcpy(copy, sp630ePayload_, sizeof(copy));
  portEXIT_CRITICAL(&payloadMux_);
  return String(copy);
}

bool TouchBleClient::send(BlueSquidControl::Command command, uint8_t target,
                          uint16_t value) {
  if (command == BlueSquidControl::Command::SetRgbw) {
    const uint8_t zone = target >> 4;
    const uint8_t channel = target & 0x0F;
    if (!connected() || zone > 3 || channel > 3 || value > 100) return false;
    prepareRgbwState(zone);
    rgbwRequested_[zone].channels[channel] = static_cast<uint8_t>(value);
    if (!rgbwPending_[zone]) rgbwChangedMs_[zone] = millis();
    rgbwPending_[zone] = true;
    return true;
  }
  if (command == BlueSquidControl::Command::SetRgbwPreset) {
    const uint8_t zone = target >> 4;
    const uint8_t field = target & 0x0F;
    if (!connected() || zone > 3 || field > 4 || value > 100) return false;
    prepareRgbwState(zone);
    if (field < 3)
      rgbwRequested_[zone].colour[field] = static_cast<uint8_t>(value);
    else if (field == 3)
      rgbwRequested_[zone].brightness = static_cast<uint8_t>(value);
    else
      rgbwRequested_[zone].options = static_cast<uint8_t>(value);
    if (!rgbwPending_[zone]) rgbwChangedMs_[zone] = millis();
    rgbwPending_[zone] = true;
    return true;
  }
  return sendValueCommand(command, target, value);
}

bool TouchBleClient::requestAction(BlueSquidControl::Command command,
                                   uint16_t value) {
  actionResult_.store(0);
  actionCommand_.store(static_cast<uint8_t>(command));
  actionSequence_.store(commandSequence_);
  return sendValueCommand(command, 0, value);
}

bool TouchBleClient::sendValueCommand(BlueSquidControl::Command command,
                                      uint8_t target, uint16_t value) {
  if (!connected() || commandCharacteristic_ == nullptr) return false;
  uint8_t packet[BlueSquidBle::kCommandHeaderSize + 2]{};
  const size_t length = BlueSquidBle::encodeValueCommand(
      commandSequence_++, command, target, value, packet);
  return commandCharacteristic_->writeValue(packet, length, false);
}

void TouchBleClient::prepareRgbwState(uint8_t zone) {
  if (rgbwPending_[zone] || rgbwAwaitingAck_[zone] || rgbwSnapshotGate_[zone].waiting) return;
  auto& request = rgbwRequested_[zone];
  memcpy(request.channels, status_.rgbw[zone], 4);
  memcpy(request.colour, status_.rgb[zone], 3);
  request.brightness = status_.rgbwBrightness[zone];
  request.options = status_.rgbwOptions[zone];
}

bool TouchBleClient::sendRgbwState(uint8_t zone) {
  if (!connected() || commandCharacteristic_ == nullptr || zone > 3)
    return false;
  uint8_t packet[BlueSquidBle::kCommandHeaderSize + 9]{};
  const uint16_t sequence = commandSequence_++;
  packet[0] = BlueSquidBle::kProtocolVersion;
  BlueSquidBle::writeU16(packet + 1, sequence);
  packet[3] = static_cast<uint8_t>(BlueSquidControl::Command::SetRgbwState);
  packet[4] = zone;
  packet[5] = 9;
  const auto& request = rgbwRequested_[zone];
  memcpy(packet + BlueSquidBle::kCommandHeaderSize, request.channels, 4);
  memcpy(packet + BlueSquidBle::kCommandHeaderSize + 4, request.colour, 3);
  packet[BlueSquidBle::kCommandHeaderSize + 7] =
      request.brightness;
  packet[BlueSquidBle::kCommandHeaderSize + 8] = request.options;
  rgbwAckStartedMs_[zone] = millis();
  rgbwAwaitingAck_[zone] = true;
  rgbwSequence_[zone] = sequence;
  if (!commandCharacteristic_->writeValue(packet, sizeof(packet), false)) {
    rgbwAwaitingAck_[zone] = false;
    return false;
  }
  rgbwPending_[zone] = false;
  return true;
}

bool TouchBleClient::connected() const {
  return !connectionInProgress_.load() &&
         client_ != nullptr && client_->isConnected() &&
         status_.lastHeartbeatMs != 0 &&
         millis() - status_.lastHeartbeatMs <= BlueSquidBle::kOnlineTimeoutMs;
}

void TouchBleClient::snapshotNotification(NimBLERemoteCharacteristic*,
                                          uint8_t* data, size_t length,
                                          bool) {
  if (activeClient != nullptr) activeClient->processSnapshot(data, length);
}

void TouchBleClient::ackNotification(NimBLERemoteCharacteristic*,
                                     uint8_t* data, size_t length, bool) {
  if (activeClient != nullptr) activeClient->processAck(data, length);
}

void TouchBleClient::processSnapshot(const uint8_t* data, size_t length) {
  if (data == nullptr || length != BlueSquidBle::kSnapshotSize ||
      data[BlueSquidBle::kSnapshotVersion] !=
          BlueSquidBle::kProtocolVersion) {
    return;
  }
  status_.sp630eAssigned = data[BlueSquidBle::kSnapshotSp630eAssigned];
  status_.shoreValid = data[BlueSquidBle::kSnapshotShoreValid] != 0;
  status_.shorePower = status_.shoreValid ? BlueSquidBle::readU16(
      data + BlueSquidBle::kSnapshotShorePower) : 0;
  status_.shoreState = status_.shoreValid ? data[BlueSquidBle::kSnapshotShoreState] : 255;
  status_.sp630eAvailable = data[BlueSquidBle::kSnapshotSp630eAvailable];
  const uint8_t validity = data[BlueSquidBle::kSnapshotValidity];
  status_.rearUptimeSeconds =
      BlueSquidBle::readU32(data + BlueSquidBle::kSnapshotUptime);
  status_.rearFirmwareMajor = data[BlueSquidBle::kSnapshotFirmware];
  status_.rearFirmwareMinor = data[BlueSquidBle::kSnapshotFirmware + 1];
  status_.rearFirmwarePatch = data[BlueSquidBle::kSnapshotFirmware + 2];
  status_.systemInfoValid = true;
  status_.energyValid = (validity & BlueSquidBle::kBatteryValid) != 0;
  status_.inverterValid = (validity & BlueSquidBle::kInverterValid) != 0;
  status_.inverterMode = data[BlueSquidBle::kSnapshotInverterMode];
  const uint8_t outputFlags = data[BlueSquidBle::kSnapshotOutputFlags];
  status_.usb = (outputFlags & 1) != 0;
  status_.pump = (outputFlags & 2) != 0;
  status_.accessory3 = (outputFlags & 4) != 0;
  status_.accessory4 = (outputFlags & 8) != 0;
  status_.fan = data[BlueSquidBle::kSnapshotFan];
  status_.fanFlags = data[BlueSquidBle::kSnapshotFanFlags];
  status_.fanPreset = data[BlueSquidBle::kSnapshotFanPreset];
  status_.fanSource = data[BlueSquidBle::kSnapshotFanSource];
  status_.fanInstance = data[BlueSquidBle::kSnapshotFanInstance];
  status_.fanError = data[BlueSquidBle::kSnapshotFanError];
  const uint32_t revision = BlueSquidBle::readU32(data + BlueSquidBle::kSnapshotRevision);
  uint8_t previousLights[4][4];
  memcpy(previousLights, status_.rgbw, sizeof(previousLights));
  if (!rgbwPending_[0] && !rgbwAwaitingAck_[0] && rgbwSnapshotGate_[0].accepts(revision)) {
    memcpy(status_.rgbw[0], data + BlueSquidBle::kSnapshotFrontRgbw, 4);
    memcpy(status_.rgb[0], data + BlueSquidBle::kSnapshotFrontPreset, 3);
    status_.rgbwBrightness[0] =
        data[BlueSquidBle::kSnapshotFrontPreset + 3];
    status_.rgbwOptions[0] = data[BlueSquidBle::kSnapshotFrontPreset + 4];
  }
  if (!rgbwPending_[1] && !rgbwAwaitingAck_[1] && rgbwSnapshotGate_[1].accepts(revision)) {
    memcpy(status_.rgbw[1], data + BlueSquidBle::kSnapshotRearRgbw, 4);
    memcpy(status_.rgb[1], data + BlueSquidBle::kSnapshotRearPreset, 3);
    status_.rgbwBrightness[1] =
        data[BlueSquidBle::kSnapshotRearPreset + 3];
    status_.rgbwOptions[1] = data[BlueSquidBle::kSnapshotRearPreset + 4];
  }
  for (uint8_t zone = 2; zone < 4; ++zone) {
    if (rgbwPending_[zone] || rgbwAwaitingAck_[zone] || !rgbwSnapshotGate_[zone].accepts(revision)) continue;
    const uint8_t* extra = data + BlueSquidBle::kSnapshotExtraRgbw + (zone - 2) * 9;
    memcpy(status_.rgbw[zone], extra, 4);
    memcpy(status_.rgb[zone], extra + 4, 3);
    status_.rgbwBrightness[zone] = extra[7];
    status_.rgbwOptions[zone] = extra[8];
    status_.rgbwPresetValid[zone] = true;
  }
  status_.rgbwPresetValid[0] = true;
  status_.rgbwPresetValid[1] = true;
  for (uint8_t zone = 0; zone < 4; ++zone) {
    if (memcmp(previousLights[zone], status_.rgbw[zone], 4) == 0) continue;
    Serial.printf("BLE light %u snapshot revision=%lu RGB=%u,%u,%u W=%u options=%u available=%u\n",
                  zone + 1, static_cast<unsigned long>(revision),
                  status_.rgbw[zone][0], status_.rgbw[zone][1], status_.rgbw[zone][2],
                  status_.rgbw[zone][3], status_.rgbwOptions[zone], status_.outputAvailable(zone));
  }
  status_.voltage =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotVoltage) / 1000.0F;
  status_.current =
      BlueSquidBle::readI16(data + BlueSquidBle::kSnapshotCurrent) / 100.0F;
  status_.soc =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotSoc) / 10.0F;
  status_.batteryPower =
      BlueSquidBle::readI16(data + BlueSquidBle::kSnapshotBatteryPower);
  status_.solarPower =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotSolarPower);
  status_.dcDcPower =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotDcDcPower);
  status_.loadPower =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotLoadPower);
  status_.remainingAh =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotRemainingAh) / 10.0F;
  status_.timeToGoMinutes =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotTimeToGo);
  status_.solarState = data[BlueSquidBle::kSnapshotChargerStates];
  status_.dcDcState = data[BlueSquidBle::kSnapshotChargerStates + 1];
  status_.cabinTemperatureC = BlueSquidBle::readI16(
      data + BlueSquidBle::kSnapshotCabinTemperature) / 10.0F;
  status_.fridgeTemperatureC = BlueSquidBle::readI16(
      data + BlueSquidBle::kSnapshotFridgeTemperature) / 10.0F;
  status_.humidity =
      BlueSquidBle::readU16(data + BlueSquidBle::kSnapshotHumidity) / 10.0F;
  status_.pitchDegrees =
      BlueSquidBle::readI16(data + BlueSquidBle::kSnapshotPitch) / 100.0F;
  status_.rollDegrees =
      BlueSquidBle::readI16(data + BlueSquidBle::kSnapshotRoll) / 100.0F;
  status_.levelValid = (validity & BlueSquidBle::kSensorsValid) != 0;
  status_.batteryCapacityAh = BlueSquidBle::readU16(
      data + BlueSquidBle::kSnapshotBatteryCapacity) / 10.0F;
  status_.pitchZeroDegrees =
      BlueSquidBle::readI16(data + BlueSquidBle::kSnapshotPitchZero) / 100.0F;
  status_.rollZeroDegrees =
      BlueSquidBle::readI16(data + BlueSquidBle::kSnapshotRollZero) / 100.0F;
  status_.settingsValid = true;
  status_.lastHeartbeatMs = millis();
  ++receivedSnapshotCount_;
  ++connectionProgress_; // Only validated snapshots count as reconnect progress.
}

void TouchBleClient::processAck(const uint8_t* data, size_t length) {
  if (data == nullptr || length != BlueSquidBle::kAckSize ||
      data[0] != BlueSquidBle::kProtocolVersion) {
    return;
  }
  lastAckRevision_ = BlueSquidBle::readU32(data + 5);
  if (data[3] == static_cast<uint8_t>(BlueSquidControl::Command::SetInverter) ||
      data[3] == static_cast<uint8_t>(BlueSquidControl::Command::SetCharger)) {
    Serial.printf("Power control ACK: command=%u result=%u revision=%lu\n",
                  static_cast<unsigned>(data[3]),
                  static_cast<unsigned>(data[4]),
                  static_cast<unsigned long>(lastAckRevision_));
  }
  if (data[3] == actionCommand_.load() &&
      BlueSquidBle::readU16(data + 1) == actionSequence_.load()) {
    actionResult_.store(
        data[4] == static_cast<uint8_t>(BlueSquidBle::AckResult::Accepted) ? 1 : 2);
  }
  if (data[3] ==
      static_cast<uint8_t>(BlueSquidControl::Command::SetRgbwState)) {
    const uint8_t zone = data[9];
    const uint16_t sequence = BlueSquidBle::readU16(data + 1);
    if (zone < 4 && rgbwSequence_[zone] == sequence) {
      Serial.printf("BLE light %u ACK sequence=%u result=%u revision=%lu elapsed=%lu ms\n",
                    zone + 1, sequence, data[4], static_cast<unsigned long>(lastAckRevision_),
                    static_cast<unsigned long>(millis() - rgbwAckStartedMs_[zone]));
      if (data[4] == static_cast<uint8_t>(BlueSquidBle::AckResult::Accepted))
        rgbwSnapshotGate_[zone].expect(lastAckRevision_);
      else
        rgbwSnapshotGate_[zone] = {};
      rgbwAwaitingAck_[zone] = false;
      if (data[4] != static_cast<uint8_t>(
                         BlueSquidBle::AckResult::Accepted)) {
        sendValueCommand(BlueSquidControl::Command::RequestStatus, 0, 0);
      }
    }
  }
  ++receivedAckCount_;
}
