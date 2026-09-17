#include "BatteryManager.h"

#include <WiFi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "AppConfig.h"
#include "Logging.h"

namespace {
constexpr char kTag[] = "CerboModbus";
constexpr uint32_t kStaleMs = 5000;
constexpr uint32_t kPollIntervalMs = 2000;
constexpr uint16_t kModbusPort = 502;
constexpr uint8_t kSystemUnitId = 100;
constexpr uint8_t kDefaultVebusUnitId = 227;
constexpr uint16_t kFirstRegister = 840;
constexpr uint16_t kRegisterCount = 21;

bool different(const BatteryStatus& a, const BatteryStatus& b) {
  return memcmp(&a, &b, sizeof(BatteryStatus)) != 0;
}

bool parseFloat(const char* value, float& destination) {
  if (value == nullptr) return false;
  char* end = nullptr;
  const float parsed = strtof(value, &end);
  if (end == value || *end != '\0' || !isfinite(parsed)) return false;
  destination = parsed;
  return true;
}
}  // namespace

struct BatteryManager::CerboPort {
  explicit CerboPort(volatile uint8_t& vebusUnitId)
      : vebusUnitId(vebusUnitId) {}

  volatile uint8_t& vebusUnitId;
  WiFiClient client;
  IPAddress address;
  uint16_t transactionId = 1;
  uint32_t lastPollMs = 0;
  uint32_t lastFrameMs = 0;
  BatteryStatus received{};

  static int16_t signedRegister(const uint16_t* values, uint16_t address_) {
    return static_cast<int16_t>(values[address_ - kFirstRegister]);
  }

  bool readExact(uint8_t* destination, size_t length, uint32_t timeoutMs) {
    const uint32_t started = millis();
    size_t receivedBytes = 0;
    while (receivedBytes < length && millis() - started < timeoutMs) {
      while (client.available() && receivedBytes < length)
        destination[receivedBytes++] = client.read();
      vTaskDelay(pdMS_TO_TICKS(2));
    }
    return receivedBytes == length;
  }

  bool readRegisterBlock(uint8_t unitId, uint16_t first, uint16_t count,
                         uint16_t* values, uint16_t valueBase) {
    if (count == 0 || count > 7) return false;
    if (!client.connect(address, kModbusPort, 350)) return false;
    const uint16_t id = transactionId++;
    const uint8_t request[] = {
        static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id), 0, 0, 0, 6,
        unitId, 3, static_cast<uint8_t>(first >> 8),
        static_cast<uint8_t>(first), static_cast<uint8_t>(count >> 8),
        static_cast<uint8_t>(count)};
    client.write(request, sizeof(request));
    uint8_t header[9]{};
    if (!readExact(header, sizeof(header), 700) || header[0] != request[0] ||
        header[1] != request[1] || header[6] != unitId ||
        header[7] != 3 || header[8] != count * 2) {
      client.stop();
      return false;
    }
    uint8_t bytes[14]{};
    if (!readExact(bytes, count * 2, 700)) {
      client.stop();
      return false;
    }
    client.stop();
    for (uint16_t index = 0; index < count; ++index)
      values[first - valueBase + index] =
          (static_cast<uint16_t>(bytes[index * 2]) << 8) |
          bytes[index * 2 + 1];
    return true;
  }

  bool readSystemRegisters() {
    uint16_t values[kRegisterCount]{};
    for (uint16_t& value : values) value = 0xFFFF;
    // The GX register map has unregistered gaps at 847-849, 852-854 and
    // 856-859. A request spanning any gap is rejected, so read each defined
    // region separately. The battery block identifies the Cerbo; other blocks
    // are optional because they depend on the installed Victron equipment.
    if (!readRegisterBlock(kSystemUnitId, 840, 7, values, kFirstRegister))
      return false;
    readRegisterBlock(kSystemUnitId, 850, 2, values, kFirstRegister);
    readRegisterBlock(kSystemUnitId, 855, 1, values, kFirstRegister);
    readRegisterBlock(kSystemUnitId, 860, 1, values, kFirstRegister);
    // Victron uses 0xFFFF for an unavailable uint16 value. A system without
    // a battery monitor can still provide valid voltage/current/power while
    // SOC, consumed Ah and time-to-go are unavailable. Do not discard the
    // complete system snapshot in that case.
    if (values[0] == 0xFFFF) return false;

    BatteryStatus next = received;
    next.voltage = values[0] / 10.0F;
    next.current = signedRegister(values, 841) / 10.0F;
    next.power = signedRegister(values, 842);
    if (values[3] != 0xFFFF) next.stateOfCharge = values[3];
    next.consumedAh = values[5] == 0xFFFF ? 0.0F : values[5] / -10.0F;
    next.timeToGoMinutes =
        values[6] == 0xFFFF ? 0.0F : values[6] / 0.6F;
    next.solarPower = values[10] == 0xFFFF ? 0.0F : values[10];
    next.dcDcPower = values[15] == 0xFFFF ? 0.0F : values[15];
    // System /Dc/Vebus/Power: signed watts, positive from Multi to DC bus.
    // This is charger output, not AC input (which also supplies AC loads).
    uint16_t chargePower = 0;
    next.shoreValid = readRegisterBlock(kSystemUnitId, 866, 1, &chargePower, 866);
    next.shorePower = next.shoreValid
        ? max(0, static_cast<int>(static_cast<int16_t>(chargePower))) : 0;
    uint16_t chargeState = 255;
    next.shoreState = readRegisterBlock(vebusUnitId, 31, 1, &chargeState, 31)
        && chargeState <= 255 ? static_cast<uint8_t>(chargeState) : 255;
    next.loadPower = values[20] == 0xFFFF
                         ? max(0.0F, next.solarPower + next.dcDcPower + next.shorePower - next.power)
                         : max(0, static_cast<int>(signedRegister(values, 860)));
    next.shuntValid = values[3] != 0xFFFF;
    next.valid = true;
    next.solarValid = values[10] != 0xFFFF;
    next.dcDcValid = values[15] != 0xFFFF;
    next.solarChargerState = next.solarValid && next.solarPower > 0 ? 3 : 0;
    next.dcDcChargerState = next.dcDcValid && next.dcDcPower > 0 ? 3 : 0;
    uint16_t inverterMode = 0xFFFF;
    if (readRegisterBlock(vebusUnitId, 33, 1, &inverterMode, 33) &&
        inverterMode >= 1 && inverterMode <= 4) {
      next.inverterMode = static_cast<uint8_t>(inverterMode);
      next.inverterValid = true;
    } else {
      next.inverterValid = false;
    }
    received = next;
    lastFrameMs = millis();
    return true;
  }

  bool writeInverterMode(uint8_t mode) {
    // VE.Bus register 33 supports all four switch positions:
    // 1=charger only, 2=inverter only, 3=on, 4=off.
    if (WiFi.status() != WL_CONNECTED || address != WiFi.gatewayIP() ||
        address[0] == 0 || mode < 1 || mode > 4 ||
        !client.connect(address, kModbusPort, 350)) return false;
    const uint16_t id = transactionId++;
    const uint8_t request[] = {
        static_cast<uint8_t>(id >> 8), static_cast<uint8_t>(id), 0, 0, 0, 6,
        vebusUnitId, 6, 0, 33, 0, mode};
    client.write(request, sizeof(request));
    uint8_t response[12]{};
    const bool success = readExact(response, sizeof(response), 700) &&
                         memcmp(request, response, sizeof(request)) == 0;
    client.stop();
    return success;
  }

  void poll() {
    const uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      address = IPAddress();
      lastFrameMs = 0;
      return;
    }
    if (now - lastPollMs < kPollIntervalMs) return;
    lastPollMs = now;
    // The Cerbo hosts this network and is its DHCP gateway.
    address = WiFi.gatewayIP();
    if (address[0] == 0 || !readSystemRegisters()) {
      LOG_WARN(kTag, "Cerbo Modbus unavailable at gateway %s", address.toString().c_str());
    }
  }

  bool fresh(uint32_t now) const {
    return lastFrameMs != 0 && now - lastFrameMs <= kStaleMs;
  }
};

BatteryManager::BatteryManager(EventManager& eventManager,
                               SettingsManager& settingsManager)
    : eventManager_(eventManager), settingsManager_(settingsManager) {}

bool BatteryManager::begin() {
  capacityAh_ = settingsManager_.loadBatteryCapacityAh(AppConfig::Battery::kCapacityAh);
  if (!isfinite(capacityAh_) || capacityAh_ < 10.0F || capacityAh_ > 2000.0F)
    capacityAh_ = AppConfig::Battery::kCapacityAh;
  LOG_INFO(kTag, "Configured battery capacity %.1f Ah", capacityAh_);
  vebusUnitId_ = settingsManager_.loadCerboVebusUnitId(kDefaultVebusUnitId);
  if (vebusUnitId_ == 0 || vebusUnitId_ > 247)
    vebusUnitId_ = kDefaultVebusUnitId;
#if BLUESQUID_SIMULATED_BATTERY
  LOG_INFO(kTag, "Using simulated Cerbo energy data");
#else
  cerbo_ = new CerboPort(vebusUnitId_);
  LOG_INFO(kTag,
           "Cerbo Modbus TCP monitoring enabled (system ID 100, VE.Bus ID %u)",
           vebusUnitId_);
#endif
  return true;
}

bool BatteryManager::setVebusUnitId(uint8_t unitId) {
  if (unitId == 0 || unitId > 247 ||
      !settingsManager_.saveCerboVebusUnitId(unitId))
    return false;
  vebusUnitId_ = unitId;
  return true;
}

bool BatteryManager::startBackgroundTask() {
  if (taskHandle_ != nullptr) return true;
  return xTaskCreatePinnedToCore(taskEntry, "cerbo-rs485", 4096, this, 1,
                                 &taskHandle_, 0) == pdPASS;
}

void BatteryManager::taskEntry(void* context) {
  static_cast<BatteryManager*>(context)->taskLoop();
}

void BatteryManager::taskLoop() {
  for (;;) { update(); vTaskDelay(pdMS_TO_TICKS(10)); }
}

void BatteryManager::update() {
  BatteryStatus next = status();
  const uint32_t now = millis();
#if BLUESQUID_SIMULATED_BATTERY
  next.voltage = 13.2F; next.current = 8.4F; next.power = 110.9F;
  next.stateOfCharge = 76.0F; next.solarPower = 185.0F; next.dcDcPower = 110.0F;
  next.consumedAh = -216.0F; next.timeToGoMinutes = 1080.0F;
  next.shuntValid = next.solarValid = next.dcDcValid = next.valid = true;
  next.solarChargerState = 3; next.dcDcChargerState = 5;
#else
  const int8_t requestedMode = pendingInverterMode_;
  if (requestedMode >= 0) {
    pendingInverterMode_ = -1;
    if (!cerbo_->writeInverterMode(static_cast<uint8_t>(requestedMode)))
      LOG_WARN(kTag, "Failed to set VE.Bus inverter mode");
    else
      LOG_INFO(kTag, "VE.Bus inverter mode set to %d", requestedMode);
  }
  cerbo_->poll();
  if (cerbo_->fresh(millis())) {
    const float solarEnergy = next.solarEnergyWh;
    const float dcDcEnergy = next.dcDcEnergyWh;
    const float loadEnergy = next.loadEnergyWh;
    next = cerbo_->received;
    next.solarEnergyWh = solarEnergy;
    next.dcDcEnergyWh = dcDcEnergy;
    next.loadEnergyWh = loadEnergy;
  } else {
    next.shuntValid = next.solarValid = next.dcDcValid = next.valid = false;
    next.shoreValid = false;
    next.shorePower = 0;
    next.shoreState = 255;
    next.solarChargerState = next.dcDcChargerState = 255;
  }
#endif
  next.remainingAh = constrain(capacityAh_ + next.consumedAh, 0.0F, capacityAh_);
  if (!next.valid)
    next.loadPower = max(0.0F, next.solarPower + next.dcDcPower - next.power);
  updateEnergyTotals(next, now);
  commitStatus(next);
}

void BatteryManager::updateEnergyTotals(BatteryStatus& status, uint32_t now) {
  if (lastEnergyMs_ != 0) {
    const float hours = min<uint32_t>(now - lastEnergyMs_, 5000U) / 3600000.0F;
    status.solarEnergyWh += max(0.0F, status.solarPower) * hours;
    status.dcDcEnergyWh += max(0.0F, status.dcDcPower) * hours;
    status.loadEnergyWh += max(0.0F, status.loadPower) * hours;
  }
  lastEnergyMs_ = now;
}

BatteryStatus BatteryManager::status() const {
  portENTER_CRITICAL(&statusMux_); BatteryStatus copy = status_;
  portEXIT_CRITICAL(&statusMux_); return copy;
}

bool BatteryManager::consumeStatusChanged() {
  portENTER_CRITICAL(&statusMux_); const bool changed = statusChanged_;
  statusChanged_ = false; portEXIT_CRITICAL(&statusMux_); return changed;
}

void BatteryManager::commitStatus(const BatteryStatus& status) {
  portENTER_CRITICAL(&statusMux_);
  if (different(status_, status)) { status_ = status; statusChanged_ = true; }
  portEXIT_CRITICAL(&statusMux_);
}

float BatteryManager::capacityAh() const {
  portENTER_CRITICAL(&statusMux_); const float value = capacityAh_;
  portEXIT_CRITICAL(&statusMux_); return value;
}

bool BatteryManager::setCapacityAh(float capacityAh) {
  if (!isfinite(capacityAh) || capacityAh < 10.0F || capacityAh > 2000.0F) return false;
  if (!settingsManager_.saveBatteryCapacityAh(capacityAh)) return false;
  portENTER_CRITICAL(&statusMux_); capacityAh_ = capacityAh; statusChanged_ = true;
  portEXIT_CRITICAL(&statusMux_); return true;
}

bool BatteryManager::setInverterEnabled(bool enabled) {
  if (cerbo_ == nullptr) return false;
  const uint8_t current = status().inverterMode;
  const bool chargerEnabled = current == 1 || current == 3;
  pendingInverterMode_ = enabled ? (chargerEnabled ? 3 : 2)
                                 : (chargerEnabled ? 1 : 4);
  LOG_INFO(kTag, "Inverter %s requested; current mode=%u, target mode=%d",
           enabled ? "on" : "off", current, pendingInverterMode_);
  return true;
}

bool BatteryManager::setChargerEnabled(bool enabled) {
  if (cerbo_ == nullptr) return false;
  const uint8_t current = status().inverterMode;
  const bool inverterEnabled = current == 2 || current == 3;
  pendingInverterMode_ = enabled ? (inverterEnabled ? 3 : 1)
                                 : (inverterEnabled ? 2 : 4);
  LOG_INFO(kTag, "Charger %s requested; current mode=%u, target mode=%d",
           enabled ? "on" : "off", current, pendingInverterMode_);
  return true;
}
