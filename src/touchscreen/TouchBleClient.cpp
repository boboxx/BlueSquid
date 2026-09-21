#include "TouchBleClient.h"

#include <NimBLEDevice.h>

#include "BlueSquidBleProtocol.h"
#include "AppConfig.h"

namespace {
TouchBleClient* activeClient = nullptr;
constexpr char kTouchDeviceName[] = "BlueSquid-Touch";
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

  void onConnect(NimBLEClient*) override { owner_.applyConnectionParameters(); }

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
  NimBLEDevice::init(kTouchDeviceName);
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

void TouchBleClient::update() {
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
    ESP.restart();
    return;
  }
  if (recovery == BleReconnectWatchdog::Action::Disconnect) {
    Serial.printf("BLE reconnect made no progress at %s for 30 seconds; requesting disconnect\n", connectionStage_.load());
    if (client_ && client_->isConnected()) client_->disconnect();
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

void TouchBleClient::connectionTaskEntry(void* context) {
  auto* owner = static_cast<TouchBleClient*>(context);
  for (;;) {
    // NimBLE owns task notifications while synchronous GATT calls wait.
    // Use a separate semaphore for jobs so BLE completions cannot start one.
    xSemaphoreTake(owner->connectionWake_, portMAX_DELAY);
    Serial.println("BLE controller reconnect started");
    const bool success = owner->connectToRear();
    if (!success) {
      if (owner->client_ && owner->client_->isConnected()) owner->client_->disconnect();
      owner->disconnected();
    }
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
  client_->setConnectionParams(12, 24, 0, 300);
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
  if (legacyService != nullptr) {
    setConnectionStage("discovering configuration");
    config =
        legacyService->getCharacteristic(AppConfig::Ble::kConfigUuid);
    setConnectionStage("discovering device configuration");
    discovery = legacyService->getCharacteristic(
        AppConfig::Ble::kBmsDiscoveryUuid);
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
  applyConnectionParameters();
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
  connectRequested_ = false;
  status_.lastHeartbeatMs = 0;
  if (configurationQueue_) xQueueReset(configurationQueue_);
  for (uint8_t zone = 0; zone < 4; ++zone) {
    rgbwPending_[zone] = false;
    rgbwAwaitingAck_[zone] = false;
  }
  lastConnectAttemptMs_ = millis();
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
  activeClient->sp630ePayload_ = String();
  activeClient->sp630ePayload_.reserve(length);
  for (size_t index = 0; index < length; ++index)
    activeClient->sp630ePayload_ += static_cast<char>(data[index]);
  ++activeClient->sp630ePayloadRevision_;
}

bool TouchBleClient::send(BlueSquidControl::Command command, uint8_t target,
                          uint16_t value) {
  if (command == BlueSquidControl::Command::SetRgbw) {
    const uint8_t zone = target >> 4;
    const uint8_t channel = target & 0x0F;
    if (!connected() || zone > 3 || channel > 3 || value > 100) return false;
    uint8_t* channels = status_.rgbwChannels(zone);
    channels[channel] = static_cast<uint8_t>(value);
    if (!rgbwPending_[zone]) rgbwChangedMs_[zone] = millis();
    rgbwPending_[zone] = true;
    return true;
  }
  if (command == BlueSquidControl::Command::SetRgbwPreset) {
    const uint8_t zone = target >> 4;
    const uint8_t field = target & 0x0F;
    if (!connected() || zone > 3 || field > 4 || value > 100) return false;
    if (field < 3)
      status_.rgb[zone][field] = static_cast<uint8_t>(value);
    else if (field == 3)
      status_.rgbwBrightness[zone] = static_cast<uint8_t>(value);
    else
      status_.rgbwOptions[zone] = static_cast<uint8_t>(value);
    status_.rgbwPresetValid[zone] = true;
    if (!rgbwPending_[zone]) rgbwChangedMs_[zone] = millis();
    rgbwPending_[zone] = true;
    return true;
  }
  return sendValueCommand(command, target, value);
}

bool TouchBleClient::sendValueCommand(BlueSquidControl::Command command,
                                      uint8_t target, uint16_t value) {
  if (!connected() || commandCharacteristic_ == nullptr) return false;
  uint8_t packet[BlueSquidBle::kCommandHeaderSize + 2]{};
  const size_t length = BlueSquidBle::encodeValueCommand(
      commandSequence_++, command, target, value, packet);
  return commandCharacteristic_->writeValue(packet, length, false);
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
  const uint8_t* channels = status_.rgbwChannels(zone);
  memcpy(packet + BlueSquidBle::kCommandHeaderSize, channels, 4);
  memcpy(packet + BlueSquidBle::kCommandHeaderSize + 4, status_.rgb[zone], 3);
  packet[BlueSquidBle::kCommandHeaderSize + 7] =
      status_.rgbwBrightness[zone];
  packet[BlueSquidBle::kCommandHeaderSize + 8] = status_.rgbwOptions[zone];
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

void TouchBleClient::setDisplaySleeping(bool sleeping) {
  if (displaySleeping_ == sleeping) return;
  displaySleeping_ = sleeping;
  applyConnectionParameters();
}

void TouchBleClient::applyConnectionParameters() {
  if (client_ == nullptr || !client_->isConnected()) return;
  if (displaySleeping_)
    client_->updateConnParams(80, 160, 0, 400);
  else
    client_->updateConnParams(12, 24, 0, 300);
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
  if (!rgbwPending_[0] && !rgbwAwaitingAck_[0]) {
    memcpy(status_.rgbw[0], data + BlueSquidBle::kSnapshotFrontRgbw, 4);
    memcpy(status_.rgb[0], data + BlueSquidBle::kSnapshotFrontPreset, 3);
    status_.rgbwBrightness[0] =
        data[BlueSquidBle::kSnapshotFrontPreset + 3];
    status_.rgbwOptions[0] = data[BlueSquidBle::kSnapshotFrontPreset + 4];
  }
  if (!rgbwPending_[1] && !rgbwAwaitingAck_[1]) {
    memcpy(status_.rgbw[1], data + BlueSquidBle::kSnapshotRearRgbw, 4);
    memcpy(status_.rgb[1], data + BlueSquidBle::kSnapshotRearPreset, 3);
    status_.rgbwBrightness[1] =
        data[BlueSquidBle::kSnapshotRearPreset + 3];
    status_.rgbwOptions[1] = data[BlueSquidBle::kSnapshotRearPreset + 4];
  }
  for (uint8_t zone = 2; zone < 4; ++zone) {
    if (rgbwPending_[zone] || rgbwAwaitingAck_[zone]) continue;
    const uint8_t* extra = data + BlueSquidBle::kSnapshotExtraRgbw + (zone - 2) * 9;
    memcpy(status_.rgbw[zone], extra, 4);
    memcpy(status_.rgb[zone], extra + 4, 3);
    status_.rgbwBrightness[zone] = extra[7];
    status_.rgbwOptions[zone] = extra[8];
    status_.rgbwPresetValid[zone] = true;
  }
  status_.rgbwPresetValid[0] = true;
  status_.rgbwPresetValid[1] = true;
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
  if (data[3] ==
      static_cast<uint8_t>(BlueSquidControl::Command::SetRgbwState)) {
    const uint8_t zone = data[9];
    const uint16_t sequence = BlueSquidBle::readU16(data + 1);
    if (zone < 4 && rgbwSequence_[zone] == sequence) {
      rgbwAwaitingAck_[zone] = false;
      if (data[4] != static_cast<uint8_t>(
                         BlueSquidBle::AckResult::Accepted)) {
        sendValueCommand(BlueSquidControl::Command::RequestStatus, 0, 0);
      }
    }
  }
  ++receivedAckCount_;
}
