#include "BleManager.h"
#include "FirmwareUpdate.h"

#include <NimBLEDevice.h>

#include <math.h>

#include "AppConfig.h"
#include "BlueSquidBleProtocol.h"
#include "Logging.h"
#include "Sp630eBleAdapter.h"

namespace {
constexpr char kTag[] = "BLE";

enum class Command : uint8_t {
  Fan = 0x04,
  Usb = 0x05,
  Pump = 0x06,
  Accessory3 = 0x07,
  FrontRed = 0x08,
  FrontGreen = 0x09,
  FrontBlue = 0x0A,
  FrontWhite = 0x0B,
  BedRed = 0x0C,
  BedGreen = 0x0D,
  BedBlue = 0x0E,
  BedWhite = 0x0F,
  Scene = 0x10,
  RequestStatus = 0x20,
  ClientRole = 0x30,
  Heartbeat = 0x31,
  CalibrateLevel = 0x32,
  // 0x33 is retired; do not reuse it for another command.
};

uint16_t scaledUnsigned(float value, float scale) {
  const float scaled = value * scale;
  return static_cast<uint16_t>(constrain(lroundf(scaled), 0L, 65535L));
}

int16_t scaledSigned(float value, float scale) {
  const long scaled = lroundf(value * scale);
  return static_cast<int16_t>(constrain(scaled, -32768L, 32767L));
}

void writeUInt16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8U);
}

void writeInt16(uint8_t* destination, int16_t value) {
  writeUInt16(destination, static_cast<uint16_t>(value));
}

void writeUInt32(uint8_t* destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8U);
  destination[2] = static_cast<uint8_t>(value >> 16U);
  destination[3] = static_cast<uint8_t>(value >> 24U);
}

void writeInt32(uint8_t* destination, int32_t value) {
  writeUInt32(destination, static_cast<uint32_t>(value));
}

const char* enabledText(bool enabled) {
  return enabled ? "ON" : "OFF";
}
}  // namespace

class BleServerCallbacks final : public NimBLEServerCallbacks {
 public:
  explicit BleServerCallbacks(BleManager& manager) : manager_(manager) {}

  void onConnect(NimBLEServer* server, ble_gap_conn_desc* description) override {
    const uint8_t count = static_cast<uint8_t>(server->getConnectedCount());
    manager_.registerClient(description->conn_handle);
    manager_.setConnectedClientCount(count);
    LOG_INFO(kTag, "Client connected (%u/%u)", count,
             CONFIG_BT_NIMBLE_MAX_CONNECTIONS);

    if (count < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
      server->startAdvertising();
    }
  }

  void onDisconnect(NimBLEServer* server, ble_gap_conn_desc* description) override {
    const uint8_t count = static_cast<uint8_t>(server->getConnectedCount());
    manager_.unregisterClient(description->conn_handle);
    manager_.setConnectedClientCount(count);
    LOG_INFO(kTag, "Client disconnected (%u/%u)", count,
             CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
    if (!server->startAdvertising()) {
      LOG_WARN(kTag, "Advertising restart failed; will retry from main loop");
    }
  }

  void onAuthenticationComplete(ble_gap_conn_desc* description) override {
    if (!description->sec_state.encrypted) {
      LOG_INFO(kTag, "Connection %u using unencrypted local control session",
               description->conn_handle);
      return;
    }

    LOG_INFO(kTag, "Connection %u secured (bonded=%s)",
             description->conn_handle,
             description->sec_state.bonded ? "yes" : "no");
  }

 private:
  BleManager& manager_;
};

class BleCommandCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit BleCommandCallbacks(BleManager& manager) : manager_(manager) {}

  void onWrite(NimBLECharacteristic* characteristic,
               ble_gap_conn_desc* description) override {
    const std::string value = characteristic->getValue();
    if (value.size() != 3 ||
        static_cast<uint8_t>(value[0]) != AppConfig::Ble::kProtocolVersion) {
      LOG_WARN(kTag, "Rejected malformed command (%u bytes)",
               static_cast<unsigned>(value.size()));
      return;
    }

    const uint8_t opcode = static_cast<uint8_t>(value[1]);
    const uint8_t commandValue = static_cast<uint8_t>(value[2]);
    manager_.recordClientActivity(description->conn_handle);

    if (opcode == static_cast<uint8_t>(Command::ClientRole)) {
      manager_.setClientPrimary(description->conn_handle, commandValue != 0);
      return;
    }

    if (opcode == static_cast<uint8_t>(Command::Heartbeat)) {
      return;
    }

    manager_.queueCommand(description->conn_handle, opcode, commandValue);
  }

 private:
  BleManager& manager_;
};

class BleStatusCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit BleStatusCallbacks(BleManager& manager) : manager_(manager) {}

  void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc* description,
                   uint16_t subscriptionValue) override {
    manager_.recordClientActivity(description->conn_handle);
    LOG_INFO(kTag, "Connection %u status notifications %s",
             description->conn_handle,
             subscriptionValue != 0 ? "enabled" : "disabled");

    if (subscriptionValue != 0) {
      manager_.publishLatestStatus();
    }
  }

 private:
  BleManager& manager_;
};

class BleTouchCommandCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit BleTouchCommandCallbacks(BleManager& manager) : manager_(manager) {}

  void onWrite(NimBLECharacteristic* characteristic,
               ble_gap_conn_desc* description) override {
    const std::string value = characteristic->getValue();
    manager_.recordClientActivity(description->conn_handle);
    manager_.setClientPrimary(description->conn_handle, true);
    manager_.queueTouchCommand(
        description->conn_handle,
        reinterpret_cast<const uint8_t*>(value.data()), value.size());
  }

 private:
  BleManager& manager_;
};

class BleTouchSnapshotCallbacks final
    : public NimBLECharacteristicCallbacks {
 public:
  explicit BleTouchSnapshotCallbacks(BleManager& manager)
      : manager_(manager) {}

  void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc* description,
                   uint16_t subscriptionValue) override {
    manager_.recordClientActivity(description->conn_handle);
    manager_.setClientPrimary(description->conn_handle,
                              subscriptionValue != 0);
    if (subscriptionValue != 0 && manager_.hasLatestStatus_) {
      manager_.publishTouchSnapshot(manager_.latestStatus_, true);
    }
  }

 private:
  BleManager& manager_;
};

class BleConfigCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit BleConfigCallbacks(BleManager& manager) : manager_(manager) {}

  void onRead(NimBLECharacteristic* characteristic,
              ble_gap_conn_desc* description) override {
    manager_.recordClientActivity(description->conn_handle);
    char value[24] = {};
    snprintf(value, sizeof(value), "capacityAh=%.1f",
             manager_.batteryManager_.capacityAh());
    characteristic->setValue(value);
  }

  void onWrite(NimBLECharacteristic* characteristic,
               ble_gap_conn_desc* description) override {
    manager_.recordClientActivity(description->conn_handle);
    const std::string value = characteristic->getValue();
    if (value.rfind("sp630e.", 0) == 0)
      manager_.setSp630eConfig(value);
    else if (value.rfind("rvc.save=", 0) == 0)
      manager_.setRvcFanConfig(value);
    else if (value.rfind("wifi.ap=", 0) == 0)
      manager_.setCerboWifiConfig(value);
    else
      manager_.setBatteryCapacityConfig(value);
  }

 private:
  BleManager& manager_;
};

class BleOtaCredentialsCallbacks final : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* characteristic, ble_gap_conn_desc* description) override {
    uint8_t ack[4]{};
    if (description && description->sec_state.encrypted)
      OtaCredentials::writeId(ack, FirmwareUpdate::credentialsAcknowledgement());
    characteristic->setValue(ack, sizeof(ack));
  }
  void onWrite(NimBLECharacteristic* characteristic, ble_gap_conn_desc* description) override {
    if (!description || !description->sec_state.encrypted) return;
    const std::string bytes = characteristic->getValue();
    OtaCredentials::Value value;
    uint32_t id = 0;
    if (OtaCredentials::decode(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), value, id))
      FirmwareUpdate::setHotspotCredentials(value, id);
    // Never expose the written password as a readable characteristic value.
    uint8_t ack[4]{};
    OtaCredentials::writeId(ack, FirmwareUpdate::credentialsAcknowledgement());
    characteristic->setValue(ack, sizeof(ack));
  }
};

class BleOtaLinkCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit BleOtaLinkCallbacks(CerboWifiManager& wifi) : wifi_(wifi) {}
  void onRead(NimBLECharacteristic* characteristic, ble_gap_conn_desc* description) override {
    if (!description || !description->sec_state.encrypted) return;
    uint8_t bytes[OtaLink::kStatusSize]{};
    OtaLink::encode(bytes, wifi_.updateNetworkStatus());
    characteristic->setValue(bytes, sizeof(bytes));
  }
  void onWrite(NimBLECharacteristic* characteristic, ble_gap_conn_desc* description) override {
    if (!description || !description->sec_state.encrypted) return;
    const auto bytes = characteristic->getValue();
    if (bytes.size() == 5 && static_cast<uint8_t>(bytes[0]) <= 1)
      wifi_.requestUpdateNetwork(bytes[0] != 0, OtaCredentials::readId(
          reinterpret_cast<const uint8_t*>(bytes.data()) + 1));
  }
 private:
  CerboWifiManager& wifi_;
};

class BleDiscoveryCallbacks final : public NimBLECharacteristicCallbacks {
 public:
  explicit BleDiscoveryCallbacks(BleManager& manager) : manager_(manager) {}

  void onRead(NimBLECharacteristic* characteristic,
              ble_gap_conn_desc* description) override {
    manager_.recordClientActivity(description->conn_handle);
    characteristic->setValue(manager_.latestDiscoveryPayload_.c_str());
  }

  void onWrite(NimBLECharacteristic* characteristic,
               ble_gap_conn_desc* description) override {
    manager_.recordClientActivity(description->conn_handle);
    if (characteristic->getValue() == "sp630e")
      manager_.requestSp630eDiscovery();
    else if (characteristic->getValue() == "sp630e.config") {
      manager_.latestDiscoveryPayload_ = manager_.sp630eConfig();
      characteristic->setValue(manager_.latestDiscoveryPayload_.c_str());
      characteristic->notify(
          reinterpret_cast<const uint8_t*>(
              manager_.latestDiscoveryPayload_.c_str()),
          manager_.latestDiscoveryPayload_.length());
    } else if (characteristic->getValue() == "rvc.config") {
      manager_.latestDiscoveryPayload_ = manager_.rvcFanConfig();
      characteristic->setValue(manager_.latestDiscoveryPayload_.c_str());
      characteristic->notify();
    } else if (characteristic->getValue() == "wifi.config") {
      manager_.latestDiscoveryPayload_ = manager_.cerboWifiConfig();
      characteristic->setValue(manager_.latestDiscoveryPayload_.c_str());
      characteristic->notify(
          reinterpret_cast<const uint8_t*>(manager_.latestDiscoveryPayload_.c_str()),
          manager_.latestDiscoveryPayload_.length());
    } else {
      LOG_WARN(kTag, "Rejected unknown discovery request");
    }
  }

 private:
  BleManager& manager_;
};

BleManager::BleManager(EventManager& eventManager,
                       OutputController& outputController,
                       SensorManager& sensorManager,
                       BatteryManager& batteryManager,
                       SettingsManager& settingsManager,
                       CerboWifiManager& cerboWifi)
    : eventManager_(eventManager),
      outputController_(outputController),
      sensorManager_(sensorManager),
      batteryManager_(batteryManager),
      settingsManager_(settingsManager),
      cerboWifi_(cerboWifi) {}

bool BleManager::begin() {
  char deviceName[24] = {};
  const uint16_t deviceSuffix = static_cast<uint16_t>(ESP.getEfuseMac());
  snprintf(deviceName, sizeof(deviceName), "%s-%04X",
           AppConfig::kBleDeviceName, deviceSuffix);

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(Sp630eProtocol::kPreferredMtu);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  // Bonding plus Secure Connections prevents a new unauthenticated client
  // from silently replacing the installed touchscreen.  With no keyboard or
  // display in the rear cabinet this uses BLE "Just Works" encryption.
  NimBLEDevice::setSecurityAuth(true, false, true);
  const int bondCount = NimBLEDevice::getNumBonds();
  LOG_INFO(kTag, "Retaining %d BLE bond(s)", bondCount);

  server_ = NimBLEDevice::createServer();
  server_->setCallbacks(new BleServerCallbacks(*this));

  NimBLEService* service = server_->createService(AppConfig::Ble::kServiceUuid);
  statusCharacteristic_ = service->createCharacteristic(
      AppConfig::Ble::kStatusUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  statusCharacteristic_->setCallbacks(new BleStatusCallbacks(*this));

  NimBLECharacteristic* commandCharacteristic = service->createCharacteristic(
      AppConfig::Ble::kCommandUuid,
      NIMBLE_PROPERTY::WRITE);
  commandCharacteristic->setCallbacks(new BleCommandCallbacks(*this));

  configCharacteristic_ = service->createCharacteristic(
      AppConfig::Ble::kConfigUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
  char capacityConfig[24] = {};
  snprintf(capacityConfig, sizeof(capacityConfig), "capacityAh=%.1f",
           batteryManager_.capacityAh());
  configCharacteristic_->setValue(capacityConfig);
  configCharacteristic_->setCallbacks(new BleConfigCallbacks(*this));

  discoveryCharacteristic_ = service->createCharacteristic(
      AppConfig::Ble::kBmsDiscoveryUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::NOTIFY);
  discoveryCharacteristic_->setValue("");
  discoveryCharacteristic_->setCallbacks(new BleDiscoveryCallbacks(*this));

  NimBLECharacteristic* otaCredentials = service->createCharacteristic(
      OtaCredentials::kUuid, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
          NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC);
  otaCredentials->setCallbacks(new BleOtaCredentialsCallbacks());
  const uint8_t emptyAck[4]{};
  otaCredentials->setValue(emptyAck, sizeof(emptyAck));
  auto* otaLink = service->createCharacteristic(OtaLink::kUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
      NIMBLE_PROPERTY::READ_ENC | NIMBLE_PROPERTY::WRITE_ENC);
  otaLink->setCallbacks(new BleOtaLinkCallbacks(cerboWifi_));
  service->start();

  NimBLEService* touchService =
      server_->createService(BlueSquidBle::kServiceUuid);
  touchSnapshotCharacteristic_ = touchService->createCharacteristic(
      BlueSquidBle::kSnapshotUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::READ_ENC);
  touchSnapshotCharacteristic_->setCallbacks(
      new BleTouchSnapshotCallbacks(*this));
  NimBLECharacteristic* touchCommandCharacteristic =
      touchService->createCharacteristic(
          BlueSquidBle::kCommandUuid,
          NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
              NIMBLE_PROPERTY::WRITE_ENC);
  touchCommandCharacteristic->setCallbacks(
      new BleTouchCommandCallbacks(*this));
  touchAckCharacteristic_ = touchService->createCharacteristic(
      BlueSquidBle::kAckUuid,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY |
          NIMBLE_PROPERTY::READ_ENC);
  touchService->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(AppConfig::Ble::kServiceUuid);
  // Keep the legacy service in the limited advertising payload so existing
  // iOS discovery continues to work. The touchscreen matches this UUID,
  // then discovers its dedicated service after connecting.
  advertising->setScanResponse(true);
  advertising->start();

  LOG_INFO(kTag, "Advertising as %s", deviceName);
  return true;
}

void BleManager::update() {
  if (sp630eDiscoveryRequested_ && Sp630eBleAdapter::requestDiscovery()) {
    sp630eDiscoveryRequested_ = false;
    sp630eDiscoveryRunning_ = true;
    sp630eDiscoveryStartedMs_ = millis();
  }
  if (sp630eDiscoveryRunning_ &&
      millis() - sp630eDiscoveryStartedMs_ >= 5500) {
    sp630eDiscoveryRunning_ = false;
    latestDiscoveryPayload_ = "sp630e=";
    for (uint8_t index = 0; index < Sp630eBleAdapter::discoveredCount();
         ++index) {
      if (index != 0) latestDiscoveryPayload_ += ';';
      latestDiscoveryPayload_ += Sp630eBleAdapter::discoveredAddress(index);
      latestDiscoveryPayload_ += ',';
      latestDiscoveryPayload_ += Sp630eBleAdapter::discoveredRssi(index);
    }
    discoveryCharacteristic_->setValue(latestDiscoveryPayload_.c_str());
    if (connectedClientCount_ > 0)
      discoveryCharacteristic_->notify(
          reinterpret_cast<const uint8_t*>(latestDiscoveryPayload_.c_str()),
          latestDiscoveryPayload_.length());
  }

  PendingCommand command;
  while (dequeueCommand(command)) {
    processCommand(command.connectionHandle, command.opcode, command.value);
  }
  processTouchCommand();
  disconnectIdleClients();
  maintainAdvertising();
  if (restartRequested_) {
    delay(150);
    ESP.restart();
  }
}

void BleManager::requestSp630eDiscovery() {
  latestDiscoveryPayload_ = "";
  discoveryCharacteristic_->setValue("");
  sp630eDiscoveryRequested_ = true;
  LOG_INFO(kTag, "SP630E discovery requested");
}

String BleManager::sp630eConfig() const {
  Sp630eAssignment assignments[kSp630eAssignmentCount]{};
  const_cast<SettingsManager&>(settingsManager_)
      .loadSp630eAssignments(assignments);
  String result = "sp630e.map=";
  for (uint8_t target = 0; target < kSp630eAssignmentCount; ++target) {
    if (target != 0) result += ';';
    result += target;
    result += ',';
    result += assignments[target].channel;
    result += ',';
    result += assignments[target].address;
  }
  result += ";group," + String(const_cast<SettingsManager&>(settingsManager_).loadLightGroupMask());
  return result;
}

void BleManager::setSp630eConfig(const std::string& value) {
  if (value.rfind("sp630e.save=", 0) == 0) {
    Sp630eAssignment assignments[8]{}; uint8_t group = 15;
    if (!parseSp630eConfiguration(value.c_str() + 12, assignments, group)) {
      discoveryCharacteristic_->setValue("sp630e.error=Invalid or conflicting assignments");
      discoveryCharacteristic_->notify(); return;
    }
    if (!settingsManager_.saveSp630eConfiguration(assignments, group)) {
      discoveryCharacteristic_->setValue("sp630e.error=Could not save configuration");
      discoveryCharacteristic_->notify(); return;
    }
    restartRequested_ = true;
    return;
  }

  constexpr char prefix[] = "sp630e.assign=";
  if (value.rfind(prefix, 0) != 0) return;
  unsigned target = 0, channel = 0;
  char address[18]{};
  if (sscanf(value.c_str() + sizeof(prefix) - 1, "%u,%u,%17s", &target,
             &channel, address) != 3 || target >= kSp630eAssignmentCount ||
      (channel > 4 && !(target < 4 && Sp630eChannels::colourType(channel)))) {
    LOG_WARN(kTag, "Rejected malformed SP630E assignment");
    return;
  }
  Sp630eAssignment assignments[kSp630eAssignmentCount]{};
  settingsManager_.loadSp630eAssignments(assignments);
  const bool clearing = strcmp(address, "none") == 0;
  if (!clearing) {
    for (uint8_t index = 0; index < kSp630eAssignmentCount; ++index) {
      if (index == target || assignments[index].address[0] == '\0' ||
          strcasecmp(assignments[index].address, address) != 0) continue;
      const bool fullConflict = Sp630eChannels::colourType(assignments[index].channel) || Sp630eChannels::colourType(channel);
      const bool channelConflict = assignments[index].channel == channel;
      if (fullConflict || channelConflict) {
        LOG_WARN(kTag, "Rejected conflicting SP630E assignment");
        return;
      }
    }
  }
  memset(&assignments[target], 0, sizeof(assignments[target]));
  assignments[target].channel = static_cast<uint8_t>(channel);
  if (!clearing) strlcpy(assignments[target].address, address, 18);
  if (settingsManager_.saveSp630eAssignments(assignments))
    restartRequested_ = true;
}

String BleManager::rvcFanConfig() const {
  const auto c = const_cast<SettingsManager&>(settingsManager_).loadRvcFanConfiguration();
  return "rvc.config=" + String(c.enabled) + "|" + String(c.instance) + "|" + String(c.source);
}
void BleManager::setRvcFanConfig(const std::string& value) {
  unsigned enabled, instance, source; int end = 0;
  if (sscanf(value.c_str(), "rvc.save=%u|%u|%u%n", &enabled, &instance, &source, &end) != 3 ||
      end != value.size() || enabled > 1 || instance < 1 || instance > 250 || source < 151 || source > 159) {
    discoveryCharacteristic_->setValue("rvc.error=Invalid fan configuration");
    discoveryCharacteristic_->notify(); return;
  }
  RvcFan::Config c; c.enabled = enabled; c.instance = instance; c.source = source;
  if (!settingsManager_.saveRvcFanConfiguration(c)) {
    discoveryCharacteristic_->setValue("rvc.error=Could not save configuration");
    discoveryCharacteristic_->notify(); return;
  }
  discoveryCharacteristic_->setValue("rvc.saved"); discoveryCharacteristic_->notify();
}

String BleManager::cerboWifiConfig() const {
  return "wifi.config=" + cerboWifi_.ssid() + "|" +
         (cerboWifi_.active() ? "1" : "0") + "|" +
         String(0) + "|" + cerboWifi_.ipAddress() +
         "|" + String(batteryManager_.vebusUnitId());
}

void BleManager::setCerboWifiConfig(const std::string& value) {
  constexpr char prefix[] = "wifi.ap=";
  const String body(value.c_str() + sizeof(prefix) - 1);
  const int separator = body.indexOf('|');
  const int unitSeparator = body.indexOf('|', separator + 1);
  const String password = unitSeparator < 0
                              ? body.substring(separator + 1)
                              : body.substring(separator + 1, unitSeparator);
  const int unitId = unitSeparator < 0
                         ? batteryManager_.vebusUnitId()
                         : body.substring(unitSeparator + 1).toInt();
  if (separator <= 0 || unitId < 1 || unitId > 247 ||
      !batteryManager_.setVebusUnitId(static_cast<uint8_t>(unitId)) ||
      !cerboWifi_.configure(body.substring(0, separator), password))
    LOG_WARN(kTag, "Rejected invalid Cerbo hotspot credentials");
}

void BleManager::publishStatus(const SystemStatus& status) {
  if (statusCharacteristic_ == nullptr) {
    return;
  }

  uint8_t packet[AppConfig::Ble::kStatusPacketSize] = {};
  encodeStatus(status, packet);
  latestStatus_ = status;
  hasLatestStatus_ = true;
  statusCharacteristic_->setValue(packet, sizeof(packet));
  if (connectedClientCount_ > 0) {
    statusCharacteristic_->notify(packet, sizeof(packet));
  }
  ++touchStateRevision_;
  publishTouchSnapshot(status, connectedClientCount_ > 0);
}

bool BleManager::isClientConnected() const {
  return connectedClientCount_ > 0;
}

uint8_t BleManager::connectedClientCount() const {
  return connectedClientCount_;
}

void BleManager::publishTouchSnapshot(const SystemStatus& status,
                                      bool notify) {
  if (touchSnapshotCharacteristic_ == nullptr) return;
  uint8_t packet[BlueSquidBle::kSnapshotSize]{};
  packet[BlueSquidBle::kSnapshotVersion] = BlueSquidBle::kProtocolVersion;
  packet[BlueSquidBle::kSnapshotSp630eAssigned] = status.sp630eAssigned;
  packet[BlueSquidBle::kSnapshotSp630eAvailable] = status.sp630eAvailable;
  packet[BlueSquidBle::kSnapshotValidity] =
      (status.battery.valid ? BlueSquidBle::kBatteryValid : 0) |
      (status.battery.shuntValid ? BlueSquidBle::kShuntValid : 0) |
      (status.battery.solarValid ? BlueSquidBle::kSolarValid : 0) |
      (status.battery.dcDcValid ? BlueSquidBle::kDcDcValid : 0) |
      (status.battery.inverterValid ? BlueSquidBle::kInverterValid : 0) |
      (status.sensors.valid ? BlueSquidBle::kSensorsValid : 0);
  BlueSquidBle::writeU32(packet + BlueSquidBle::kSnapshotRevision,
                         touchStateRevision_);
  BlueSquidBle::writeU32(packet + BlueSquidBle::kSnapshotUptime,
                         status.uptimeSeconds);
  packet[BlueSquidBle::kSnapshotFirmware] = AppConfig::kFirmwareVersionMajor;
  packet[BlueSquidBle::kSnapshotFirmware + 1] =
      AppConfig::kFirmwareVersionMinor;
  packet[BlueSquidBle::kSnapshotFirmware + 2] =
      AppConfig::kFirmwareVersionPatch;
  packet[BlueSquidBle::kSnapshotOutputFlags] =
      (status.outputs.usbEnabled ? 1 : 0) |
      (status.outputs.waterPumpEnabled ? 2 : 0) |
      (status.outputs.accessory3Enabled ? 4 : 0) |
      (status.outputs.accessory4Enabled ? 8 : 0);
  packet[BlueSquidBle::kSnapshotFan] = status.outputs.fanSpeed;
  packet[BlueSquidBle::kSnapshotFanFlags] = status.outputs.fanFlags;
  packet[BlueSquidBle::kSnapshotFanPreset] = status.outputs.fanPreset;
  packet[BlueSquidBle::kSnapshotFanSource] = status.outputs.fanSource;
  packet[BlueSquidBle::kSnapshotFanInstance] = status.outputs.fanInstance;
  packet[BlueSquidBle::kSnapshotFanError] = status.outputs.fanError;
  const uint8_t front[4] = {
      status.outputs.rgbw[0][0], status.outputs.rgbw[0][1],
      status.outputs.rgbw[0][2], status.outputs.rgbw[0][3]};
  const uint8_t rear[4] = {
      status.outputs.rgbw[1][0], status.outputs.rgbw[1][1],
      status.outputs.rgbw[1][2], status.outputs.rgbw[1][3]};
  for (uint8_t zone = 2; zone < 4; ++zone) {
    uint8_t* extra = packet + BlueSquidBle::kSnapshotExtraRgbw + (zone - 2) * 9;
    memcpy(extra, status.outputs.rgbw[zone], 4);
    memcpy(extra + 4, status.outputs.rgb[zone], 3);
    extra[7] = status.outputs.rgbwBrightness[zone];
    extra[8] = status.outputs.rgbwOptions[zone];
  }
  memcpy(packet + BlueSquidBle::kSnapshotFrontRgbw, front, 4);
  memcpy(packet + BlueSquidBle::kSnapshotRearRgbw, rear, 4);
  memcpy(packet + BlueSquidBle::kSnapshotFrontPreset,
         status.outputs.rgb[0], 3);
  packet[BlueSquidBle::kSnapshotFrontPreset + 3] =
      status.outputs.rgbwBrightness[0];
  packet[BlueSquidBle::kSnapshotFrontPreset + 4] =
      status.outputs.rgbwOptions[0];
  memcpy(packet + BlueSquidBle::kSnapshotRearPreset,
         status.outputs.rgb[1], 3);
  packet[BlueSquidBle::kSnapshotRearPreset + 3] =
      status.outputs.rgbwBrightness[1];
  packet[BlueSquidBle::kSnapshotRearPreset + 4] =
      status.outputs.rgbwOptions[1];
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotVoltage,
                         scaledUnsigned(status.battery.voltage, 1000.0F));
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotCurrent,
                         scaledSigned(status.battery.current, 100.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotSoc,
                         scaledUnsigned(status.battery.stateOfCharge, 10.0F));
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotBatteryPower,
                         scaledSigned(status.battery.power, 1.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotSolarPower,
                         scaledUnsigned(status.battery.solarPower, 1.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotDcDcPower,
                         scaledUnsigned(status.battery.dcDcPower, 1.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotShorePower,
                         scaledUnsigned(status.battery.shorePower, 1.0F));
  packet[BlueSquidBle::kSnapshotShoreState] = status.battery.shoreState;
  packet[BlueSquidBle::kSnapshotShoreValid] = status.battery.shoreValid;
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotLoadPower,
                         scaledUnsigned(status.battery.loadPower, 1.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotRemainingAh,
                         scaledUnsigned(status.battery.remainingAh, 10.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotTimeToGo,
                         scaledUnsigned(status.battery.timeToGoMinutes, 1.0F));
  packet[BlueSquidBle::kSnapshotChargerStates] =
      status.battery.solarChargerState;
  packet[BlueSquidBle::kSnapshotChargerStates + 1] =
      status.battery.dcDcChargerState;
  packet[BlueSquidBle::kSnapshotInverterMode] = status.battery.inverterMode;
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotCabinTemperature,
                         scaledSigned(status.sensors.cabinTemperatureC,
                                      10.0F));
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotFridgeTemperature,
                         scaledSigned(status.sensors.fridgeTemperatureC,
                                      10.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotHumidity,
                         scaledUnsigned(status.sensors.cabinHumidityPercent,
                                        10.0F));
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotPitch,
                         scaledSigned(status.sensors.pitchDegrees, 100.0F));
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotRoll,
                         scaledSigned(status.sensors.rollDegrees, 100.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotBatteryCapacity,
                         scaledUnsigned(batteryManager_.capacityAh(), 10.0F));
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotPitchZero,
                         scaledSigned(sensorManager_.pitchZeroDegrees(),
                                      100.0F));
  BlueSquidBle::writeI16(packet + BlueSquidBle::kSnapshotRollZero,
                         scaledSigned(sensorManager_.rollZeroDegrees(),
                                      100.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotEnergyTotals,
                         scaledUnsigned(status.battery.solarEnergyWh, 1.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotEnergyTotals + 2,
                         scaledUnsigned(status.battery.dcDcEnergyWh, 1.0F));
  BlueSquidBle::writeU16(packet + BlueSquidBle::kSnapshotEnergyTotals + 4,
                         scaledUnsigned(status.battery.loadEnergyWh, 1.0F));
  touchSnapshotCharacteristic_->setValue(packet, sizeof(packet));
  if (notify) touchSnapshotCharacteristic_->notify(packet, sizeof(packet));
}

void BleManager::queueTouchCommand(uint16_t connectionHandle,
                                   const uint8_t* data, size_t length) {
  uint16_t sequence = data != nullptr && length >= 3
                          ? BlueSquidBle::readU16(data + 1)
                          : 0;
  uint8_t command = data != nullptr && length >= 4 ? data[3] : 0;
  uint8_t target = data != nullptr && length >= 5 ? data[4] : 0;
  if (data == nullptr || length < BlueSquidBle::kCommandHeaderSize ||
      data[0] != BlueSquidBle::kProtocolVersion ||
      data[5] > BlueSquidBle::kMaximumCommandPayload ||
      length != BlueSquidBle::kCommandHeaderSize + data[5]) {
    acknowledgeTouchCommand(connectionHandle, sequence, command, target,
                            static_cast<uint8_t>(
                                BlueSquidBle::AckResult::Malformed));
    return;
  }

  bool full = false;
  portENTER_CRITICAL(&commandQueueMux_);
  if (touchCommandQueueCount_ == kCommandQueueCapacity) {
    full = true;
  } else {
    PendingTouchCommand& queued = touchCommandQueue_[touchCommandQueueHead_];
    queued.connectionHandle = connectionHandle;
    queued.sequence = sequence;
    queued.command = command;
    queued.target = target;
    queued.length = data[5];
    memcpy(queued.payload, data + BlueSquidBle::kCommandHeaderSize,
           queued.length);
    touchCommandQueueHead_ = static_cast<uint8_t>(
        (touchCommandQueueHead_ + 1) % kCommandQueueCapacity);
    ++touchCommandQueueCount_;
  }
  portEXIT_CRITICAL(&commandQueueMux_);
  if (full) {
    acknowledgeTouchCommand(connectionHandle, sequence, command, target,
                            static_cast<uint8_t>(BlueSquidBle::AckResult::Busy));
  }
}

void BleManager::processTouchCommand() {
  PendingTouchCommand queued{};
  while (true) {
    bool available = false;
    portENTER_CRITICAL(&commandQueueMux_);
    if (touchCommandQueueCount_ > 0) {
      queued = touchCommandQueue_[touchCommandQueueTail_];
      touchCommandQueueTail_ = static_cast<uint8_t>(
          (touchCommandQueueTail_ + 1) % kCommandQueueCapacity);
      --touchCommandQueueCount_;
      available = true;
    }
    portEXIT_CRITICAL(&commandQueueMux_);
    if (!available) break;

    BlueSquidBle::AckResult result = BlueSquidBle::AckResult::Accepted;
    const auto command = static_cast<BlueSquidControl::Command>(queued.command);
    const uint16_t value = queued.length >= 2
                               ? BlueSquidBle::readU16(queued.payload)
                               : 0;
    switch (command) {
      case BlueSquidControl::Command::SetRgbw: {
        const uint8_t zone = queued.target >> 4;
        const uint8_t channel = queued.target & 0x0F;
        if (queued.length != 2 || zone > 3 || channel > 3 || value > 100 ||
            !outputController_.setRgbwChannel(static_cast<RgbwZone>(zone),
                                              channel, value))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      }
      case BlueSquidControl::Command::SetRgbwPreset: {
        const uint8_t zone = queued.target >> 4;
        const uint8_t field = queued.target & 0x0F;
        if (queued.length != 2 || zone > 3 || field > 4 || value > 100 ||
            !outputController_.setRgbwPresetField(
                static_cast<RgbwZone>(zone), field, value))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      }
      case BlueSquidControl::Command::SetRgbwState:
        if (queued.length != 9 || queued.target > 3 ||
            !outputController_.setRgbwState(
                static_cast<RgbwZone>(queued.target), queued.payload,
                queued.payload + 4, queued.payload[7], queued.payload[8]))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      case BlueSquidControl::Command::SetFanReverse:
        if (queued.length != 2 || value > 1 ||
            !outputController_.setFanReverse(value != 0))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      case BlueSquidControl::Command::SetFan:
        if (queued.length != 2 || value > 100 ||
            !outputController_.setFanSpeed(value))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      case BlueSquidControl::Command::SetUsb:
        if (queued.length != 2 || value > 1)
          result = BlueSquidBle::AckResult::InvalidValue;
        else
          outputController_.setUsbEnabled(value != 0);
        break;
      case BlueSquidControl::Command::SetPump:
        if (queued.length != 2 || value > 1)
          result = BlueSquidBle::AckResult::InvalidValue;
        else
          outputController_.setWaterPumpEnabled(value != 0);
        break;
      case BlueSquidControl::Command::SetAccessory3:
        if (queued.length != 2 || value > 1)
          result = BlueSquidBle::AckResult::InvalidValue;
        else
          outputController_.setAccessory3Enabled(value != 0);
        break;
      case BlueSquidControl::Command::SetAccessory4:
        if (queued.length != 2 || value > 1)
          result = BlueSquidBle::AckResult::InvalidValue;
        else
          outputController_.setAccessory4Enabled(value != 0);
        break;
      case BlueSquidControl::Command::SetInverter:
        if (queued.length != 2 || value > 1 ||
            !batteryManager_.setInverterEnabled(value != 0))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      case BlueSquidControl::Command::SetCharger:
        if (queued.length != 2 || value > 1 ||
            !batteryManager_.setChargerEnabled(value != 0))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      case BlueSquidControl::Command::ApplyScene:
        if (queued.length != 2 ||
            value > static_cast<uint8_t>(LightingScene::Travel))
          result = BlueSquidBle::AckResult::InvalidValue;
        else
          outputController_.applyScene(static_cast<LightingScene>(value));
        break;
      case BlueSquidControl::Command::RequestStatus:
        break;
      case BlueSquidControl::Command::SetAllLights:
        if (queued.length != 2 || value > 1)
          result = BlueSquidBle::AckResult::InvalidValue;
        else
          outputController_.setAllLightsEnabled(value != 0);
        break;
      case BlueSquidControl::Command::CalibrateLevel:
        if (!sensorManager_.calibrateLevel())
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      case BlueSquidControl::Command::SetBatteryCapacity:
        if (queued.length != 2 ||
            !batteryManager_.setCapacityAh(value / 10.0F))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      case BlueSquidControl::Command::SetLevelCalibration: {
        const float setting = static_cast<int16_t>(value) / 100.0F;
        if (queued.length != 2 || queued.target > 3 ||
            !sensorManager_.setLevelCalibration(
                queued.target == 0 ? setting
                                   : sensorManager_.pitchZeroDegrees(),
                queued.target == 1 ? setting
                                   : sensorManager_.rollZeroDegrees()))
          result = BlueSquidBle::AckResult::InvalidValue;
        break;
      }
      default:
        result = BlueSquidBle::AckResult::InvalidCommand;
        break;
    }

    if (result == BlueSquidBle::AckResult::Accepted) {
      SystemStatus current = latestStatus_;
      current.outputs = outputController_.status();
      current.sensors = sensorManager_.status();
      current.battery = batteryManager_.status();
      current.uptimeSeconds = millis() / 1000U;
      publishStatus(current);
    }
    acknowledgeTouchCommand(queued.connectionHandle, queued.sequence,
                            queued.command, queued.target,
                            static_cast<uint8_t>(result));
  }
}

void BleManager::acknowledgeTouchCommand(uint16_t connectionHandle,
                                         uint16_t sequence, uint8_t command,
                                         uint8_t target, uint8_t result) {
  if (touchAckCharacteristic_ == nullptr) return;
  uint8_t packet[BlueSquidBle::kAckSize]{};
  packet[0] = BlueSquidBle::kProtocolVersion;
  BlueSquidBle::writeU16(packet + 1, sequence);
  packet[3] = command;
  packet[4] = result;
  BlueSquidBle::writeU32(packet + 5, touchStateRevision_);
  packet[9] = target;
  touchAckCharacteristic_->setValue(packet, sizeof(packet));
  if (connectedClientCount_ > 0)
    touchAckCharacteristic_->notify(packet, sizeof(packet));
  recordClientActivity(connectionHandle);
}

void BleManager::setBatteryCapacityConfig(const std::string& value) {
  constexpr char prefix[] = "capacityAh=";
  if (value.rfind(prefix, 0) != 0) {
    LOG_WARN(kTag, "Rejected malformed energy config");
    return;
  }
  char* end = nullptr;
  const float capacity = strtof(value.c_str() + strlen(prefix), &end);
  if (end == nullptr || *end != '\0' || !batteryManager_.setCapacityAh(capacity)) {
    LOG_WARN(kTag, "Rejected battery capacity config");
    return;
  }
  char encoded[24] = {};
  snprintf(encoded, sizeof(encoded), "capacityAh=%.1f", capacity);
  if (configCharacteristic_ != nullptr) configCharacteristic_->setValue(encoded);
}

void BleManager::queueCommand(uint16_t connectionHandle, uint8_t opcode,
                              uint8_t value) {
  recordClientActivity(connectionHandle);
  bool droppedOldest = false;

  portENTER_CRITICAL(&commandQueueMux_);
  if (commandQueueCount_ == kCommandQueueCapacity) {
    commandQueueTail_ =
        static_cast<uint8_t>((commandQueueTail_ + 1) % kCommandQueueCapacity);
    --commandQueueCount_;
    droppedOldest = true;
  }

  commandQueue_[commandQueueHead_].opcode = opcode;
  commandQueue_[commandQueueHead_].value = value;
  commandQueue_[commandQueueHead_].connectionHandle = connectionHandle;
  commandQueueHead_ =
      static_cast<uint8_t>((commandQueueHead_ + 1) % kCommandQueueCapacity);
  ++commandQueueCount_;
  portEXIT_CRITICAL(&commandQueueMux_);

  if (droppedOldest) {
    LOG_WARN(kTag, "BLE command queue full; oldest command discarded");
  }
}

void BleManager::registerClient(uint16_t connectionHandle) {
  portENTER_CRITICAL(&clientMux_);
  for (ConnectedClient& client : clients_) {
    if (!client.connected) {
      client.connectionHandle = connectionHandle;
      client.lastActivityMs = millis();
      client.primary = false;
      client.connected = true;
      break;
    }
  }
  portEXIT_CRITICAL(&clientMux_);
}

void BleManager::unregisterClient(uint16_t connectionHandle) {
  portENTER_CRITICAL(&clientMux_);
  for (ConnectedClient& client : clients_) {
    if (client.connected && client.connectionHandle == connectionHandle) {
      client = ConnectedClient{};
      break;
    }
  }
  portEXIT_CRITICAL(&clientMux_);
}

void BleManager::recordClientActivity(uint16_t connectionHandle) {
  portENTER_CRITICAL(&clientMux_);
  for (ConnectedClient& client : clients_) {
    if (client.connected && client.connectionHandle == connectionHandle) {
      client.lastActivityMs = millis();
      break;
    }
  }
  portEXIT_CRITICAL(&clientMux_);
}

void BleManager::setClientPrimary(uint16_t connectionHandle, bool primary) {
  portENTER_CRITICAL(&clientMux_);
  for (ConnectedClient& client : clients_) {
    if (client.connected && client.connectionHandle == connectionHandle) {
      client.primary = primary;
      client.lastActivityMs = millis();
      break;
    }
  }
  portEXIT_CRITICAL(&clientMux_);

  LOG_INFO(kTag, "Connection %u role = %s", connectionHandle,
           primary ? "primary" : "secondary");
}

void BleManager::maintainAdvertising() {
  if (server_ == nullptr) return;
  const uint32_t now = millis();
  if (now - lastAdvertisingCheckMs_ < 5000) return;
  lastAdvertisingCheckMs_ = now;

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  const unsigned peers = server_->getConnectedCount();
  // A callback restart can fail transiently. Retry without resetting the
  // shared BLE host, which would also drop the SP630E connections. NimBLE
  // checks host readiness and reports failures (including shared capacity).
  if (!advertising->isAdvertising() &&
      peers < CONFIG_BT_NIMBLE_MAX_CONNECTIONS) {
    const bool started = advertising->start();
    LOG_INFO(kTag, "Advertising recovery: %s; server clients=%u",
             started ? "started" : "failed; retry in 5s", peers);
  }
  if (now - lastAdvertisingDiagnosticMs_ >= 30000) {
    lastAdvertisingDiagnosticMs_ = now;
    LOG_INFO(kTag, "BLE health: advertising=%s server clients=%u scanning=%s",
             advertising->isAdvertising() ? "yes" : "no", peers,
             NimBLEDevice::getScan()->isScanning() ? "yes" : "no");
  }
}

void BleManager::disconnectIdleClients() {
  if (server_ == nullptr) {
    return;
  }

  uint16_t idleHandles[kMaximumClients] = {};
  uint8_t idleCount = 0;
  const uint32_t now = millis();

  portENTER_CRITICAL(&clientMux_);
  for (ConnectedClient& client : clients_) {
    if (client.connected && !client.primary &&
        now - client.lastActivityMs >=
            AppConfig::Ble::kSecondaryClientIdleTimeoutMs) {
      idleHandles[idleCount++] = client.connectionHandle;
      client.lastActivityMs = now;
    }
  }
  portEXIT_CRITICAL(&clientMux_);

  for (uint8_t index = 0; index < idleCount; ++index) {
    LOG_INFO(kTag, "Disconnecting idle secondary client %u",
             idleHandles[index]);
    server_->disconnect(idleHandles[index]);
  }
}

bool BleManager::dequeueCommand(PendingCommand& command) {
  bool available = false;

  portENTER_CRITICAL(&commandQueueMux_);
  if (commandQueueCount_ > 0) {
    command = commandQueue_[commandQueueTail_];
    commandQueueTail_ =
        static_cast<uint8_t>((commandQueueTail_ + 1) % kCommandQueueCapacity);
    --commandQueueCount_;
    available = true;
  }
  portEXIT_CRITICAL(&commandQueueMux_);

  return available;
}

void BleManager::processCommand(uint16_t connectionHandle, uint8_t opcode,
                                uint8_t value) {
  const uint8_t percent = constrain(value, 0, 100);
  switch (static_cast<Command>(opcode)) {
    case Command::Fan:
      outputController_.setFanSpeed(percent);
      LOG_INFO(kTag, "Client %u command: ventilation fan = %u%%",
               connectionHandle, percent);
      break;
    case Command::Usb:
      outputController_.setUsbEnabled(value != 0);
      LOG_INFO(kTag, "Client %u command: USB charging = %s",
               connectionHandle, enabledText(value != 0));
      break;
    case Command::Pump:
      outputController_.setWaterPumpEnabled(value != 0);
      LOG_INFO(kTag, "Client %u command: water pump = %s",
               connectionHandle, enabledText(value != 0));
      break;
    case Command::Accessory3:
      outputController_.setAccessory3Enabled(value != 0);
      LOG_INFO(kTag, "Client %u command: accessory 3 = %s",
               connectionHandle, enabledText(value != 0));
      break;
    case Command::FrontRed:
      outputController_.setRgbwChannel(RgbwZone::Output1, 0, percent);
      LOG_INFO(kTag, "Client %u command: front red = %u%%",
               connectionHandle, percent);
      break;
    case Command::FrontGreen:
      outputController_.setRgbwChannel(RgbwZone::Output1, 1, percent);
      LOG_INFO(kTag, "Client %u command: front green = %u%%",
               connectionHandle, percent);
      break;
    case Command::FrontBlue:
      outputController_.setRgbwChannel(RgbwZone::Output1, 2, percent);
      LOG_INFO(kTag, "Client %u command: front blue = %u%%",
               connectionHandle, percent);
      break;
    case Command::FrontWhite:
      outputController_.setRgbwChannel(RgbwZone::Output1, 3, percent);
      LOG_INFO(kTag, "Client %u command: front white = %u%%",
               connectionHandle, percent);
      break;
    case Command::BedRed:
      outputController_.setRgbwChannel(RgbwZone::Output2, 0, percent);
      LOG_INFO(kTag, "Client %u command: bed red = %u%%",
               connectionHandle, percent);
      break;
    case Command::BedGreen:
      outputController_.setRgbwChannel(RgbwZone::Output2, 1, percent);
      LOG_INFO(kTag, "Client %u command: bed green = %u%%",
               connectionHandle, percent);
      break;
    case Command::BedBlue:
      outputController_.setRgbwChannel(RgbwZone::Output2, 2, percent);
      LOG_INFO(kTag, "Client %u command: bed blue = %u%%",
               connectionHandle, percent);
      break;
    case Command::BedWhite:
      outputController_.setRgbwChannel(RgbwZone::Output2, 3, percent);
      LOG_INFO(kTag, "Client %u command: bed white = %u%%",
               connectionHandle, percent);
      break;
    case Command::Scene:
      if (value <= static_cast<uint8_t>(LightingScene::Travel)) {
        outputController_.applyScene(static_cast<LightingScene>(value));
        static const char* kSceneNames[] = {"Day", "Camp", "Night", "Travel"};
        LOG_INFO(kTag, "Client %u command: scene = %s", connectionHandle,
                 kSceneNames[value]);
      } else {
        LOG_WARN(kTag, "Rejected invalid scene value %u", value);
      }
      break;
    case Command::RequestStatus:
      LOG_DEBUG(kTag, "Client %u requested status", connectionHandle);
      publishLatestStatus();
      return;
    case Command::CalibrateLevel:
      if (sensorManager_.calibrateLevel()) {
        LOG_INFO(kTag, "Client %u calibrated pitch/roll level",
                 connectionHandle);
        if (hasLatestStatus_) {
          latestStatus_.sensors = sensorManager_.status();
          publishStatus(latestStatus_);
        }
      }
      return;
    default:
      LOG_WARN(kTag, "Rejected unknown command 0x%02X", opcode);
      return;
  }

  eventManager_.publish({EventType::OutputChanged, millis(), opcode});
  if (hasLatestStatus_) {
    latestStatus_.outputs = outputController_.status();
    latestStatus_.uptimeSeconds = millis() / 1000U;
    publishStatus(latestStatus_);
  }
}

void BleManager::publishLatestStatus() {
  if (hasLatestStatus_) {
    publishStatus(latestStatus_);
  }
}

void BleManager::setConnectedClientCount(uint8_t count) {
  connectedClientCount_ = count;
}

void BleManager::encodeStatus(const SystemStatus& status, uint8_t* packet) {
  packet[2] = packet[3] = packet[4] = 0;
  packet[0] = AppConfig::Ble::kProtocolVersion;
  packet[1] = (status.outputs.usbEnabled ? 0x01 : 0x00) |
              (status.outputs.waterPumpEnabled ? 0x02 : 0x00) |
              (status.outputs.accessory3Enabled ? 0x04 : 0x00) |
              (status.outputs.accessory4Enabled ? 0x08 : 0x00);
  packet[5] = status.outputs.fanSpeed;
  writeUInt16(packet + 6, scaledUnsigned(status.battery.voltage, 1000.0F));
  writeInt16(packet + 8, scaledSigned(status.battery.current, 100.0F));
  writeUInt16(packet + 10, scaledUnsigned(status.battery.stateOfCharge, 100.0F));
  writeUInt16(packet + 12, scaledUnsigned(status.battery.solarPower, 1.0F));
  writeInt16(packet + 14, scaledSigned(status.sensors.cabinTemperatureC, 100.0F));
  writeInt16(packet + 16, scaledSigned(status.sensors.fridgeTemperatureC, 100.0F));
  writeInt16(packet + 18, scaledSigned(status.sensors.pitchDegrees, 100.0F));
  writeInt16(packet + 20, scaledSigned(status.sensors.rollDegrees, 100.0F));
  writeUInt32(packet + 22, status.uptimeSeconds);
  writeUInt16(packet + 26,
              scaledUnsigned(status.sensors.cabinHumidityPercent, 100.0F));
  packet[28] = status.outputs.rgbw[0][0];
  packet[29] = status.outputs.rgbw[0][1];
  packet[30] = status.outputs.rgbw[0][2];
  packet[31] = status.outputs.rgbw[0][3];
  packet[32] = status.outputs.rgbw[1][0];
  packet[33] = status.outputs.rgbw[1][1];
  packet[34] = status.outputs.rgbw[1][2];
  packet[35] = status.outputs.rgbw[1][3];
  writeUInt16(packet + 36, scaledUnsigned(status.battery.dcDcPower, 1.0F));
  writeUInt16(packet + 38, scaledUnsigned(status.battery.loadPower, 1.0F));
  writeInt16(packet + 40, scaledSigned(status.battery.power, 1.0F));
  writeInt32(packet + 42, lroundf(status.battery.consumedAh * 1000.0F));
  writeUInt32(packet + 46, static_cast<uint32_t>(max(0L,
      lroundf(status.battery.remainingAh * 1000.0F))));
  writeUInt32(packet + 50, static_cast<uint32_t>(max(0L,
      lroundf(status.battery.timeToGoMinutes * 60.0F))));
  packet[54] = (status.battery.shuntValid ? 0x01 : 0) |
               (status.battery.solarValid ? 0x02 : 0) |
               (status.battery.dcDcValid ? 0x04 : 0);
  packet[55] = 0;
  writeUInt16(packet + 56, scaledUnsigned(status.battery.solarEnergyWh, 1.0F));
  writeUInt16(packet + 58, scaledUnsigned(status.battery.dcDcEnergyWh, 1.0F));
  writeUInt16(packet + 60, scaledUnsigned(status.battery.loadEnergyWh, 1.0F));
  packet[62] = status.battery.solarChargerState;
  packet[63] = status.battery.dcDcChargerState;
}
