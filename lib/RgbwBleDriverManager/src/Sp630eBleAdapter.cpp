#include "Sp630eBleAdapter.h"

#include <string.h>
#include "Logging.h"

namespace {
constexpr char kTag[] = "SP630E";
constexpr uint32_t kReconnectMs = 3000;
SemaphoreHandle_t connectMutex = nullptr;
portMUX_TYPE discoveryMutex = portMUX_INITIALIZER_UNLOCKED;
struct DiscoveryResult {
  char address[18]{};
  int rssi = 0;
} discoveries[8];
uint8_t discoveryCount = 0;
char scanTarget[18]{};
NimBLEAddress scanTargetAddress;
bool scanTargetFound = false;
}

class Sp630eAdvertisedCallbacks final : public NimBLEAdvertisedDeviceCallbacks {
 public:
  void onResult(NimBLEAdvertisedDevice* device) override {
    if (!device) return;
    const NimBLEAddress peer = device->getAddress();
    const std::string address = peer.toString();
    const bool sp630e = device->haveName() && device->getName().rfind("SP630E", 0) == 0;
    portENTER_CRITICAL(&discoveryMutex);
    if (scanTarget[0] && strcasecmp(scanTarget, address.c_str()) == 0) {
      scanTargetAddress = peer;
      scanTargetFound = true;
    }
    if (!sp630e) {
      portEXIT_CRITICAL(&discoveryMutex);
      return;
    }
    uint8_t index = 0;
    while (index < discoveryCount &&
           strcasecmp(discoveries[index].address, address.c_str()) != 0) ++index;
    if (index < 8) {
      strlcpy(discoveries[index].address, address.c_str(), 18);
      discoveries[index].rssi = device->getRSSI();
      if (index == discoveryCount) ++discoveryCount;
    }
    portEXIT_CRITICAL(&discoveryMutex);
  }
};
static Sp630eAdvertisedCallbacks discoveryCallbacks;

class Sp630eClientCallbacks final : public NimBLEClientCallbacks {
 public:
  explicit Sp630eClientCallbacks(Sp630eBleAdapter& owner) : owner_(owner) {}
  void onDisconnect(NimBLEClient*) override { owner_.disconnected(); }
  bool onConnParamsUpdateRequest(NimBLEClient*, const ble_gap_upd_params*) override {
    return true;
  }
 private:
  Sp630eBleAdapter& owner_;
};

Sp630eBleAdapter::Sp630eBleAdapter(const char* label, const char* address)
    : label_(label), configuredAddress_(address ? address : "") {}

void Sp630eBleAdapter::setAddress(const char* address) {
  // Assignments are loaded before begin(); changing an assignment reboots.
  if (task_ == nullptr) configuredAddress_ = address ? address : "";
}

void Sp630eBleAdapter::begin() {
  if (task_ || configuredAddress_.isEmpty()) return;
  if (!connectMutex) connectMutex = xSemaphoreCreateMutex();
  if (!connectMutex) {
    LOG_WARN(kTag, "%s: could not allocate connection lock", label_);
    return;
  }
  // Each adapter owns its client for its lifetime. Reusing another adapter's
  // disconnected client can leave stale characteristic pointers behind.
  client_ = NimBLEDevice::createClient();
  if (!client_) {
    LOG_WARN(kTag, "%s: no BLE client slot", label_);
    return;
  }
  client_->setClientCallbacks(new Sp630eClientCallbacks(*this), true);
  // Request 15-30 ms connection events instead of 45-75 ms. Keep the same
  // supervision timeout and allow peripheral parameter negotiation.
  client_->setConnectionParams(12, 24, 0, 300);
  client_->setConnectTimeout(4);
  LOG_INFO(kTag, "%s: starting status worker for %s", label_, configuredAddress_.c_str());
  if (xTaskCreate(taskEntry, "sp630e", 6144, this, 1, &task_) != pdPASS) {
    task_ = nullptr;
    NimBLEDevice::deleteClient(client_);
    client_ = nullptr;
    LOG_WARN(kTag, "%s: could not start BLE worker", label_);
  }
}

void Sp630eBleAdapter::update() {}  // GATT operations run only in the worker.
bool Sp630eBleAdapter::ready() const { return task_ != nullptr; }

bool Sp630eBleAdapter::available() const {
  portENTER_CRITICAL(&mutex_);
  const bool result = responseHealth_.available(millis());
  portEXIT_CRITICAL(&mutex_);
  return result;
}

bool Sp630eBleAdapter::reported(RgbwBleDriverState& state, uint32_t& revision) const {
  portENTER_CRITICAL(&mutex_);
  const bool result = acceptReport_ && desiredRevision_ == sentRevision_ &&
      reportCommandRevision_ == desiredRevision_ &&
      Sp630eProtocol::fresh(reportValid_, lastReportMs_, millis());
  if (result) { state = reported_; revision = reportRevision_; }
  portEXIT_CRITICAL(&mutex_);
  return result;
}

bool Sp630eBleAdapter::send(const RgbwBleDriverState& state) {
  if (!task_) return false;
  portENTER_CRITICAL(&mutex_);
  desired_ = state;
  desiredQueuedMs_ = millis();
  ++desiredRevision_;
  acceptReport_ = false;
  portEXIT_CRITICAL(&mutex_);
  return true;
}

void Sp630eBleAdapter::notification(uint8_t* data, size_t length) {
  portENTER_CRITICAL(&mutex_);
  ++notificationCount_;
  portEXIT_CRITICAL(&mutex_);
  Sp630eProtocol::Status parsed;
  if (!Sp630eProtocol::decode(data, length, parsed)) {
    // Only lighting-protocol notifications are logged here; never other GATT
    // data. Keep the capture bounded even if a device sends a large packet.
    char hex[193]{};
    const size_t count = length < 64 ? length : 64;
    for (size_t index = 0; data && index < count; ++index)
      snprintf(hex + index * 3, 4, "%02X ", data[index]);
    LOG_WARN(kTag, "%s: rejected status len=%u key=%02X config=%02X mode=%02X bytes=%s",
             label_, static_cast<unsigned>(length),
             data && length > 2 ? data[2] : 0,
             data && length > 19 ? data[19] : 0,
             data && length > 32 ? data[32] : 0, hex);
    return;
  }
  bool becameAvailable = false;
  bool traceReply = false;
  bool accepted = false;
  uint32_t requestedRevision = 0;
  uint32_t writtenRevision = 0;
  portENTER_CRITICAL(&mutex_);
  ++decodedCount_;
  queryPending_ = false;
  configuration_ = parsed.configuration;
  // A valid response proves contact even if its settings are older than the
  // latest queued gesture. Only applying the settings is revision-gated.
  responseHealth_.received(millis());
  requestedRevision = desiredRevision_;
  writtenRevision = sentRevision_;
  accepted = acceptReport_ && desiredRevision_ == sentRevision_;
  traceReply = !reportValid_ || reportCommandRevision_ != desiredRevision_ ||
      memcmp(reported_.channels, parsed.channels, sizeof(reported_.channels)) != 0 ||
      reported_.options != parsed.options || reported_.brightness != parsed.brightness;
  if (accepted) {
    warmCache_.observe(data);
    becameAvailable = !Sp630eProtocol::fresh(reportValid_, lastReportMs_, millis());
    memcpy(reported_.channels, parsed.channels, sizeof(reported_.channels));
    memcpy(reported_.color, parsed.color, 3);
    reported_.brightness = parsed.brightness;
    reported_.options = parsed.options;
    lastReportMs_ = millis();
    reportValid_ = true;
    reportCommandRevision_ = desiredRevision_;
    ++reportRevision_;
  }
  portEXIT_CRITICAL(&mutex_);
  if (traceReply) {
    LOG_INFO(kTag, "%s: feedback accepted=%u requested=%lu written=%lu config=%02X power=%u mode=%u effect=%u coexist=%u RGBlevel=%u Wlevel=%u rawRGB=%u,%u,%u rawCW=%u rawWW=%u output=%u,%u,%u,%u,%u",
             label_, accepted, static_cast<unsigned long>(requestedRevision),
             static_cast<unsigned long>(writtenRevision), parsed.configuration,
             parsed.power, parsed.mode, data[33], data[24], data[35], data[36],
             data[37], data[38], data[39], data[40], data[41],
             parsed.channels[0], parsed.channels[1], parsed.channels[2],
             parsed.channels[3], parsed.channels[4]);
  }
  if (becameAvailable)
    LOG_INFO(kTag, "%s: status confirmed power=%u mode=%u R=%u G=%u B=%u W=%u",
             label_, parsed.power, parsed.mode, parsed.channels[0],
             parsed.channels[1], parsed.channels[2], parsed.channels[3]);
}

void Sp630eBleAdapter::disconnected() {
  portENTER_CRITICAL(&mutex_);
  responseHealth_.disconnected();
  reportValid_ = false;
  queryPending_ = false;
  acceptReport_ = false;
  portEXIT_CRITICAL(&mutex_);
  LOG_INFO(kTag, "%s disconnected", label_);
}

void Sp630eBleAdapter::taskEntry(void* context) {
  static_cast<Sp630eBleAdapter*>(context)->taskLoop();
}

void Sp630eBleAdapter::taskLoop() {
  uint32_t lastAttempt = millis() - kReconnectMs;
  uint32_t lastQuery = millis() - Sp630eProtocol::kPollMs;
  uint32_t lastSend = millis() - 100;
  uint32_t connectedMs = 0;
  uint32_t acknowledgedQueries = 0;
  bool subscriptionRetried = false;
  bool alternateQueryTried = false;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(25));
    if (!client_->isConnected() || !command_) {
      if (millis() - lastAttempt < kReconnectMs) continue;
      // Only serialize connection setup, never status polling on other links.
      if (xSemaphoreTake(connectMutex, pdMS_TO_TICKS(50)) != pdTRUE) continue;
      if (NimBLEDevice::getScan()->isScanning()) {
        xSemaphoreGive(connectMutex);
        continue;
      }
      const bool connected = connect();
      xSemaphoreGive(connectMutex);
      lastAttempt = millis();
      if (!connected) {
        if (client_->isConnected()) client_->disconnect();
        command_ = nullptr;
        disconnected();
        continue;
      }
      portENTER_CRITICAL(&mutex_);
      warmCache_.reset();
      portEXIT_CRITICAL(&mutex_);
      connectedMs = millis();
      acknowledgedQueries = 0;
      subscriptionRetried = false;
      alternateQueryTried = false;
      lastQuery = millis() - Sp630eProtocol::kPollMs;
    }
    portENTER_CRITICAL(&mutex_);
    const uint32_t lastReply = responseHealth_.lastReplyOr(connectedMs);
    portEXIT_CRITICAL(&mutex_);
    if (millis() - lastReply >= Sp630eProtocol::kStaleMs) {
      portENTER_CRITICAL(&mutex_);
      const uint32_t received = notificationCount_;
      const uint32_t decoded = decodedCount_;
      const uint32_t requested = desiredRevision_;
      const uint32_t sent = sentRevision_;
      const bool accepting = acceptReport_;
      portEXIT_CRITICAL(&mutex_);
      LOG_WARN(kTag, "%s: status timed out; queryACKs=%lu notifications=%lu decoded=%lu requested=%lu sent=%lu accepting=%u",
               label_, static_cast<unsigned long>(acknowledgedQueries),
               static_cast<unsigned long>(received), static_cast<unsigned long>(decoded),
               static_cast<unsigned long>(requested), static_cast<unsigned long>(sent), accepting);
      client_->disconnect();
      command_ = nullptr;
      disconnected();
      continue;
    }
    portENTER_CRITICAL(&mutex_);
    const bool silent = notificationCount_ == 0;
    portEXIT_CRITICAL(&mutex_);
    // A CCCD readback confirms the stored flag, not delivery of notifications.
    // Give the initial queries time to reply before re-arming a silent link.
    // Retry only once per connection; never extend the availability deadline.
    if (silent && !subscriptionRetried && acknowledgedQueries >= 2 &&
        millis() - lastQuery >= 500) {
      subscriptionRetried = true;
      LOG_WARN(kTag, "%s: no notifications after two queries; re-arming FFE1 subscription", label_);
      if (!command_->unsubscribe(true)) {
        LOG_WARN(kTag, "%s: notification disable failed (BLE error %d)", label_, client_->getLastError());
        client_->disconnect();
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      if (!client_->isConnected() || !command_->subscribe(command_->canNotify(),
          [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
            notification(data, length);
          }, true)) {
        LOG_WARN(kTag, "%s: notification re-enable failed (BLE error %d)", label_, client_->getLastError());
        client_->disconnect();
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      LOG_INFO(kTag, "%s: notification subscription re-armed; retrying status query", label_);
      lastQuery = millis() - Sp630eProtocol::kPollMs;
    }
    // Probe only the read-only status query on a silent peripheral. Some
    // implementations expose both write methods but handle them differently.
    // A successful transport write never substitutes for valid status feedback.
    if (silent && subscriptionRetried && !alternateQueryTried &&
        acknowledgedQueries >= 3 && millis() - lastQuery >= 500 &&
        command_->canWriteNoResponse()) {
      alternateQueryTried = true;
      const bool queued = command_->writeValue(Sp630eProtocol::kQuery,
          sizeof(Sp630eProtocol::kQuery), false);
      LOG_WARN(kTag, "%s: read-only status probe without response queued=%u",
               label_, queued);
    }
    RgbwBleDriverState desired;
    uint32_t revision;
    uint32_t queuedMs;
    bool pending;
    portENTER_CRITICAL(&mutex_);
    desired = desired_;
    revision = desiredRevision_;
    queuedMs = desiredQueuedMs_;
    pending = revision != sentRevision_ && configuration_ != 0;
    portEXIT_CRITICAL(&mutex_);
    // Commands precede routine polls; the bounded deferral still services
    // feedback during continuous gestures. Preserve the quiet reply window.
    if (Sp630eProtocol::pollDue(millis(), lastQuery, pending)) {
      lastQuery = millis();
      portENTER_CRITICAL(&mutex_);
      acceptReport_ = desiredRevision_ == sentRevision_;
      queryPending_ = true;
      portEXIT_CRITICAL(&mutex_);
      if (!write(Sp630eProtocol::kQuery, sizeof(Sp630eProtocol::kQuery))) {
        LOG_WARN(kTag, "%s: status query write failed (BLE error %d)",
                 label_, client_->getLastError());
        client_->disconnect();
      } else ++acknowledgedQueries;
      continue;
    }
    portENTER_CRITICAL(&mutex_);
    const bool awaitingReply = queryPending_;
    portEXIT_CRITICAL(&mutex_);
    if (awaitingReply && millis() - lastQuery < 500) continue;
    if (pending && millis() - lastSend >= 100) {
      portENTER_CRITICAL(&mutex_);
      acceptReport_ = false;
      portEXIT_CRITICAL(&mutex_);
      const uint32_t sendStarted = millis();
      if (!sendCommands(desired)) {
        portENTER_CRITICAL(&mutex_);
        warmCache_.reset();
        portEXIT_CRITICAL(&mutex_);
        LOG_WARN(kTag, "%s: output write failed (BLE error %d)",
                 label_, client_->getLastError());
        client_->disconnect();
        continue;
      }
      lastSend = millis();
      auto info = client_->getConnInfo();
      LOG_INFO(kTag, "%s: command revision=%lu queue=%lu ms writes=%lu ms interval=%u x1.25ms latency=%u",
               label_, static_cast<unsigned long>(revision),
               static_cast<unsigned long>(sendStarted - queuedMs),
               static_cast<unsigned long>(lastSend - sendStarted),
               info.getConnInterval(), info.getConnLatency());
      portENTER_CRITICAL(&mutex_);
      sentRevision_ = revision;
      portEXIT_CRITICAL(&mutex_);
    }
  }
}

bool Sp630eBleAdapter::connect() {
  command_ = nullptr;
  portENTER_CRITICAL(&mutex_);
  notificationCount_ = decodedCount_ = 0;
  configuration_ = 0;
  portEXIT_CRITICAL(&mutex_);
  // A saved MAC string does not include the BLE public/random address type.
  // Rediscover it before connecting, as the original advertised-device path
  // did. Copy the address value; never retain a pointer into scan results.
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(&discoveryCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(80);
  scan->setWindow(40);
  LOG_INFO(kTag, "%s: scanning for assigned module %s", label_, configuredAddress_.c_str());
  // This synchronous overload runs in the BLE worker, not the main loop.
  portENTER_CRITICAL(&discoveryMutex);
  strlcpy(scanTarget, configuredAddress_.c_str(), sizeof(scanTarget));
  scanTargetFound = false;
  portEXIT_CRITICAL(&discoveryMutex);
  scan->start(2, false);
  portENTER_CRITICAL(&discoveryMutex);
  const bool found = scanTargetFound;
  const NimBLEAddress address = scanTargetAddress;
  scanTarget[0] = '\0';
  portEXIT_CRITICAL(&discoveryMutex);
  if (!found) {
    LOG_WARN(kTag, "%s: assigned module not advertising", label_);
    return false;
  }
  LOG_INFO(kTag, "%s: connecting to %s (address type %u)", label_,
           configuredAddress_.c_str(), address.getType());
  if (!client_->isConnected() && !client_->connect(address, true)) {
    LOG_WARN(kTag, "%s: connection failed (BLE error %d)", label_, client_->getLastError());
    return false;
  }
  // Enumerate once per boot before retaining characteristic pointers. This
  // compares service layout without writing any manufacturer configuration.
  if (!servicesLogged_) {
    servicesLogged_ = true;
    auto* services = client_->getServices(true);
    if (services) for (auto* discovered : *services) {
      LOG_INFO(kTag, "%s: GATT service=%s", label_,
               discovered->getUUID().toString().c_str());
      auto* characteristics = discovered->getCharacteristics(true);
      if (characteristics) for (auto* characteristic : *characteristics) {
        LOG_INFO(kTag, "%s: GATT characteristic=%s handle=%u read=%u write=%u writeNR=%u notify=%u indicate=%u",
                 label_, characteristic->getUUID().toString().c_str(),
                 characteristic->getHandle(), characteristic->canRead(),
                 characteristic->canWrite(), characteristic->canWriteNoResponse(),
                 characteristic->canNotify(), characteristic->canIndicate());
      }
    }
  }
  NimBLERemoteService* service = client_->getService("ffe0");
  if (!service) service = client_->getService("e0ff");
  if (!service) {
    LOG_WARN(kTag, "%s: FFE0/E0FF service missing (BLE error %d)", label_, client_->getLastError());
    return false;
  }
  command_ = service->getCharacteristic("ffe1");
  if (!command_ || (!command_->canWrite() && !command_->canWriteNoResponse())) {
    LOG_WARN(kTag, "%s: writable FFE1 characteristic missing", label_);
    return false;
  }
  LOG_INFO(kTag, "%s: FFE1 write=%u writeNR=%u notify=%u indicate=%u MTU=%u",
           label_, command_->canWrite(), command_->canWriteNoResponse(),
           command_->canNotify(), command_->canIndicate(), client_->getMTU());
  if (!command_->canNotify() && !command_->canIndicate()) {
    LOG_WARN(kTag, "%s: FFE1 has no notification/indication support", label_);
    return false;
  }
  NimBLERemoteDescriptor* cccd = command_->getDescriptor(NimBLEUUID(uint16_t(0x2902)));
  if (!cccd) {
    // NimBLE 1.4 subscribe() returns true when this descriptor is missing,
    // even though notifications were never enabled on the peripheral.
    LOG_WARN(kTag, "%s: FFE1 notification descriptor 2902 missing", label_);
    return false;
  }
  if (!command_->subscribe(command_->canNotify(),
      [this](NimBLERemoteCharacteristic*, uint8_t* data, size_t length, bool) {
        notification(data, length);
      }, true)) {
    LOG_WARN(kTag, "%s: status subscription failed (BLE error %d)", label_, client_->getLastError());
    return false;
  }
  const NimBLEAttValue enabled = cccd->readValue();
  LOG_INFO(kTag, "%s: CCCD handle=%u readback length=%u value=%02X %02X (expected %02X 00)",
           label_, cccd->getHandle(), static_cast<unsigned>(enabled.size()),
           enabled.size() > 0 ? enabled[0] : 0, enabled.size() > 1 ? enabled[1] : 0,
           command_->canNotify() ? 1 : 2);
  if (enabled.size() == 2 &&
      (enabled[0] != (command_->canNotify() ? 1 : 2) || enabled[1] != 0)) {
    LOG_WARN(kTag, "%s: peripheral did not enable requested notifications", label_);
    return false;
  }
  LOG_INFO(kTag, "%s connected to %s; polling status (diagnostics v3)", label_, configuredAddress_.c_str());
  return true;
}

bool Sp630eBleAdapter::write(const uint8_t* data, size_t length) {
  if (!command_ || !client_->isConnected()) return false;
  // BanlanX requires acknowledged writes even when FFE1 also advertises
  // write-without-response. UniLED explicitly avoids unacknowledged writes.
  // This acknowledges GATT delivery; the notification still confirms state.
  const uint32_t started = millis();
  const bool written = command_->writeValue(data, length, command_->canWrite());
  const uint32_t elapsed = millis() - started;
  if (!written || elapsed >= 200)
    LOG_WARN(kTag, "%s: GATT opcode=%02X took=%lu ms success=%u error=%d",
             label_, length > 1 ? data[1] : 0,
             static_cast<unsigned long>(elapsed), written, client_->getLastError());
  return written;
}

bool Sp630eBleAdapter::requestDiscovery() {
  // UI discovery and worker reconnection share one scanner. Defer the UI
  // request rather than stopping a worker's scan or invalidating its results.
  if (connectMutex && xSemaphoreTake(connectMutex, 0) != pdTRUE) return false;
  portENTER_CRITICAL(&discoveryMutex);
  discoveryCount = 0;
  portEXIT_CRITICAL(&discoveryMutex);
  NimBLEScan* scan = NimBLEDevice::getScan();
  if (scan->isScanning()) scan->stop();
  scan->setAdvertisedDeviceCallbacks(&discoveryCallbacks, false);
  scan->setActiveScan(true);
  scan->setInterval(80);
  scan->setWindow(40);
  const bool started = scan->start(5, nullptr, false);
  if (connectMutex) xSemaphoreGive(connectMutex);
  return started;
}
uint8_t Sp630eBleAdapter::discoveredCount() {
  portENTER_CRITICAL(&discoveryMutex);
  const uint8_t count = discoveryCount;
  portEXIT_CRITICAL(&discoveryMutex);
  return count;
}
String Sp630eBleAdapter::discoveredAddress(uint8_t index) {
  char address[18]{};
  portENTER_CRITICAL(&discoveryMutex);
  if (index < discoveryCount) memcpy(address, discoveries[index].address, 18);
  portEXIT_CRITICAL(&discoveryMutex);
  return String(address);
}
int Sp630eBleAdapter::discoveredRssi(uint8_t index) {
  portENTER_CRITICAL(&discoveryMutex);
  const int rssi = index < discoveryCount ? discoveries[index].rssi : 0;
  portEXIT_CRITICAL(&discoveryMutex);
  return rssi;
}
namespace {
void traceLightingBatch(const char* label, const Sp630eProtocol::WarmCommands& batch) {
  char summary[160]{};
  size_t used = 0;
  for (size_t index = 0; index < batch.count; ++index) {
    const auto& packet = batch.packets[index];
    if (used >= sizeof(summary) - 1) break;
    used += snprintf(summary + used, sizeof(summary) - used, "%s%02X:", index ? " " : "", packet.data[1]);
    for (size_t byte = 6; byte < packet.size && used < sizeof(summary) - 1; ++byte)
      used += snprintf(summary + used, sizeof(summary) - used, "%02X", packet.data[byte]);
  }
  LOG_INFO(kTag, "%s: written packets [%s]", label, summary);
}
}

bool Sp630eBleAdapter::sendCommands(const RgbwBleDriverState& state) {
  portENTER_CRITICAL(&mutex_);
  const uint8_t configuration = configuration_;
  portEXIT_CRITICAL(&mutex_);
  if (configuration == 0x8A) {
    const uint32_t batchStarted = millis();
    const auto commands = Sp630eProtocol::rgbWarmCommands(state.channels, state.options, state.channels[4], state.independentChannels);
    portENTER_CRITICAL(&mutex_);
    const auto changes = warmCache_.plan(commands);
    portEXIT_CRITICAL(&mutex_);
    uint32_t previousWriteStarted = 0;
    for (size_t index = 0; index < changes.count; ++index) {
      // GATT acknowledgement time already spaces commands apart. Only wait
      // for the remainder of the minimum gap when an acknowledgement is fast.
      if (index) {
        const uint32_t elapsed = millis() - previousWriteStarted;
        if (elapsed < 30) vTaskDelay(pdMS_TO_TICKS(30 - elapsed));
      }
      previousWriteStarted = millis();
      if (!write(changes.packets[index].data, changes.packets[index].size)) return false;
    }
    portENTER_CRITICAL(&mutex_);
    warmCache_.committed(commands);
    portEXIT_CRITICAL(&mutex_);
    traceLightingBatch(label_, changes);
    LOG_INFO(kTag, "%s: RGB+CCT writes=%u took=%lu ms R=%u G=%u B=%u WW=%u CW=%u",
             label_, static_cast<unsigned>(changes.count),
             static_cast<unsigned long>(millis() - batchStarted),
             state.channels[0], state.channels[1], state.channels[2], state.channels[3], state.channels[4]);
    return true;
  }
  const auto commands = configuration == 0x85
      ? Sp630eProtocol::rgbCommands(state.channels)
      : Sp630eProtocol::rgbwCommands(state.channels, state.options, state.independentChannels);
  portENTER_CRITICAL(&mutex_);
  const auto changes = warmCache_.plan(commands);
  portEXIT_CRITICAL(&mutex_);
  for (size_t index = 0; index < changes.count; ++index)
    if (!write(changes.packets[index].data, changes.packets[index].size)) return false;
  portENTER_CRITICAL(&mutex_);
  warmCache_.committed(commands);
  portEXIT_CRITICAL(&mutex_);
  traceLightingBatch(label_, changes);
  LOG_INFO(kTag, "%s updated R=%u G=%u B=%u W=%u", label_,
           state.channels[0], state.channels[1], state.channels[2],
           state.channels[3]);
  return true;
}
