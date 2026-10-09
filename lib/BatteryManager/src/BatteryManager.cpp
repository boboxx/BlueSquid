#include "BatteryManager.h"

#include <WiFi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "AppConfig.h"
#include "Logging.h"
#include "VictronAlerts.h"

namespace {
constexpr char kTag[] = "CerboModbus";
constexpr uint32_t kStaleMs = 5000;
constexpr uint32_t kPollIntervalMs = 2000;
constexpr uint32_t kAlertPollIntervalMs = 6000;
constexpr uint32_t kTimePollIntervalMs = 60000;
// A clock reading older than this is no longer reported.
constexpr uint32_t kTimeValidMs = 10UL * 60UL * 1000UL;
// Battery monitor and solar charger unit IDs depend on the installation, so
// the Controller probes each ID in small steps between energy polls.
constexpr uint32_t kAlertScanBudgetMs = 600;
constexpr uint32_t kAlertRescanMs = 10UL * 60UL * 1000UL;
constexpr uint8_t kLastUnitId = 247;
constexpr uint8_t kMaximumAlertUnits = 2;
constexpr uint8_t kAlertFailureLimit = 3;
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

  struct AlertGroup {
    uint32_t warnings = 0;
    uint32_t alarms = 0;
    uint8_t code = 0;
    uint8_t failures = 0;
    bool available = false;
  };
  AlertGroup vebusAlerts, batteryAlerts, solarAlerts;
  uint8_t batteryUnits[kMaximumAlertUnits]{};
  uint8_t solarUnits[kMaximumAlertUnits]{};
  uint8_t batteryUnitCount = 0;
  uint8_t solarUnitCount = 0;
  uint16_t nextScanUnit = 0;
  bool scanComplete = false;
  uint32_t scanCompletedMs = 0;
  // Capacity configured in the battery monitor; 0 until one reports it.
  float monitorCapacityAh = 0.0F;
  // Solar charger register 775 (/State); 255 until a charger reports it.
  uint8_t solarState = 255;
  uint32_t lastAlertPollMs = 0;
  // System register 830 (/Timestamp): UTC seconds as a 64-bit value.
  uint32_t time = 0;
  uint32_t timeMs = 0;
  uint32_t lastTimePollMs = 0;

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
    // No DC/DC charger state is read from the Cerbo; report it as unknown.
    next.dcDcChargerState = 255;
    uint16_t inverterMode = 0xFFFF;
    if (readRegisterBlock(vebusUnitId, 33, 1, &inverterMode, 33) &&
        inverterMode >= 1 && inverterMode <= 4) {
      next.inverterMode = static_cast<uint8_t>(inverterMode);
      next.inverterValid = true;
    } else {
      next.inverterValid = false;
    }
    updateAlerts(next, millis());
    readTime();
    received = next;
    lastFrameMs = millis();
    return true;
  }

  void readTime() {
    const uint32_t now = millis();
    if (lastTimePollMs != 0 && now - lastTimePollMs < kTimePollIntervalMs)
      return;
    lastTimePollMs = now;
    uint16_t words[4]{};
    if (!readRegisterBlock(kSystemUnitId, 830, 4, words, 830)) return;
    const uint64_t seconds = (static_cast<uint64_t>(words[0]) << 48) |
                             (static_cast<uint64_t>(words[1]) << 32) |
                             (static_cast<uint64_t>(words[2]) << 16) | words[3];
    // Reject an unset clock (before 2024) or a value beyond 32 bits.
    if (seconds < 1704067200ULL || seconds > UINT32_MAX) return;
    time = static_cast<uint32_t>(seconds);
    timeMs = millis();
  }

  bool knownAlertUnit(uint8_t unit) const {
    for (uint8_t index = 0; index < batteryUnitCount; ++index)
      if (batteryUnits[index] == unit) return true;
    for (uint8_t index = 0; index < solarUnitCount; ++index)
      if (solarUnits[index] == unit) return true;
    return false;
  }

  void restartAlertScan() {
    scanComplete = false;
    nextScanUnit = 0;
  }

  // Identifies battery monitors (register 259, battery voltage) and solar
  // chargers (register 771, battery voltage). The Cerbo rejects registers
  // outside a unit's service and unknown unit IDs, so each probe is quick.
  void scanAlertUnits(uint32_t now) {
    if (scanComplete) {
      const bool allFound = batteryUnitCount == kMaximumAlertUnits &&
                            solarUnitCount == kMaximumAlertUnits;
      if (allFound || now - scanCompletedMs < kAlertRescanMs) return;
      restartAlertScan();
    }
    const uint32_t started = millis();
    while (!scanComplete && millis() - started < kAlertScanBudgetMs) {
      const uint8_t unit = static_cast<uint8_t>(nextScanUnit++);
      if (nextScanUnit > kLastUnitId) {
        scanComplete = true;
        scanCompletedMs = now;
        LOG_INFO(kTag, "Alert device scan complete: %u battery monitor(s), "
                 "%u solar charger(s)", batteryUnitCount, solarUnitCount);
      }
      if (unit == kSystemUnitId || unit == vebusUnitId || knownAlertUnit(unit))
        continue;
      uint16_t value = 0;
      if (batteryUnitCount < kMaximumAlertUnits &&
          readRegisterBlock(unit, 259, 1, &value, 259)) {
        batteryUnits[batteryUnitCount++] = unit;
        LOG_INFO(kTag, "Battery monitor found at unit ID %u", unit);
      } else if (solarUnitCount < kMaximumAlertUnits &&
                 readRegisterBlock(unit, 771, 1, &value, 771)) {
        solarUnits[solarUnitCount++] = unit;
        LOG_INFO(kTag, "Solar charger found at unit ID %u", unit);
      }
    }
  }

  // Keeps the last values through brief read failures; a group that keeps
  // failing is cleared and, for discovered units, rediscovered.
  bool recordAlertGroup(AlertGroup& group, bool success, uint32_t warnings,
                        uint32_t alarms, uint8_t code) {
    if (success) {
      group.warnings = warnings;
      group.alarms = alarms;
      group.code = code;
      group.failures = 0;
      group.available = true;
      return false;
    }
    if (!group.available) return false;
    if (++group.failures < kAlertFailureLimit) return false;
    group = AlertGroup();
    return true;
  }

  void readVebusAlerts() {
    uint16_t values[16];
    for (uint16_t& value : values) value = 0xFFFF;
    // 32 error code, 34-36 alarms; 42-47 sensor and L1 alarms.
    const bool success = readRegisterBlock(vebusUnitId, 32, 5, values, 32);
    if (success) readRegisterBlock(vebusUnitId, 42, 6, values, 32);
    uint32_t warnings = 0, alarms = 0;
    using namespace VictronAlerts;
    applyRegister(warnings, alarms, kVebusHighTemperature, values[2]);
    applyRegister(warnings, alarms, kVebusLowBattery, values[3]);
    applyRegister(warnings, alarms, kVebusOverload, values[4]);
    applyRegister(warnings, alarms, kVebusTemperatureSensor, values[10]);
    applyRegister(warnings, alarms, kVebusVoltageSensor, values[11]);
    applyRegister(warnings, alarms, kVebusRipple, values[15]);
    recordAlertGroup(vebusAlerts, success, warnings, alarms,
                     clampCode(values[0]));
  }

  void readBatteryAlerts() {
    if (batteryUnitCount == 0) return;
    uint32_t warnings = 0, alarms = 0;
    bool success = false;
    float capacityAh = 0.0F;
    for (uint8_t index = 0; index < batteryUnitCount; ++index) {
      const uint8_t unit = batteryUnits[index];
      uint16_t values[12], extended[7];
      for (uint16_t& value : values) value = 0xFFFF;
      for (uint16_t& value : extended) value = 0xFFFF;
      bool read = readRegisterBlock(unit, 268, 7, values, 268);
      read |= readRegisterBlock(unit, 275, 5, values, 268);
      read |= readRegisterBlock(unit, 320, 7, extended, 320);
      uint16_t voltage = 0;
      // A monitor without alarm registers still counts as reachable.
      if (!read) read = readRegisterBlock(unit, 259, 1, &voltage, 259);
      success |= read;
      // Register 309 is /Capacity: the monitor's battery capacity, 0.1 Ah.
      uint16_t capacity = 0xFFFF;
      if (capacityAh == 0.0F && readRegisterBlock(unit, 309, 1, &capacity, 309) &&
          capacity >= 100 && capacity <= 20000)
        capacityAh = capacity / 10.0F;
      for (uint8_t offset = 0; offset < 12; ++offset)
        VictronAlerts::applyRegister(
            warnings, alarms, VictronAlerts::kBatteryLowVoltage + offset,
            values[offset]);
      for (uint8_t offset = 0; offset < 7; ++offset)
        VictronAlerts::applyRegister(
            warnings, alarms,
            VictronAlerts::kBatteryHighChargeCurrent + offset,
            extended[offset]);
    }
    if (capacityAh != 0.0F) monitorCapacityAh = capacityAh;
    if (recordAlertGroup(batteryAlerts, success, warnings, alarms, 0)) {
      LOG_WARN(kTag, "Battery monitor alerts unavailable; rescanning");
      batteryUnitCount = 0;
      monitorCapacityAh = 0.0F;
      restartAlertScan();
    }
  }

  void readSolarAlerts() {
    if (solarUnitCount == 0) return;
    uint8_t code = 0;
    uint8_t state = 255;
    bool success = false;
    for (uint8_t index = 0; index < solarUnitCount; ++index) {
      uint16_t value = 0xFFFF;
      bool read = readRegisterBlock(solarUnits[index], 788, 1, &value, 788);
      if (!read) read = readRegisterBlock(solarUnits[index], 771, 1, &value, 771);
      else if (code == 0) code = VictronAlerts::clampCode(value);
      success |= read;
      uint16_t chargeState = 0xFFFF;
      if (state == 255 &&
          readRegisterBlock(solarUnits[index], 775, 1, &chargeState, 775) &&
          chargeState < 255)
        state = static_cast<uint8_t>(chargeState);
    }
    if (success) solarState = state;
    if (recordAlertGroup(solarAlerts, success, 0, 0, code)) {
      LOG_WARN(kTag, "Solar charger alerts unavailable; rescanning");
      solarUnitCount = 0;
      solarState = 255;
      restartAlertScan();
    }
  }

  void updateAlerts(BatteryStatus& next, uint32_t now) {
    scanAlertUnits(now);
    if (lastAlertPollMs == 0 || now - lastAlertPollMs >= kAlertPollIntervalMs) {
      lastAlertPollMs = now;
      readVebusAlerts();
      readBatteryAlerts();
      readSolarAlerts();
    }
    next.alertWarnings =
        vebusAlerts.warnings | batteryAlerts.warnings | solarAlerts.warnings;
    next.alertAlarms =
        vebusAlerts.alarms | batteryAlerts.alarms | solarAlerts.alarms;
    next.vebusError = vebusAlerts.code;
    next.solarError = solarAlerts.code;
    next.solarChargerState = solarState;
    next.alertSources =
        (vebusAlerts.available ? VictronAlerts::kSourceVebus : 0) |
        (batteryAlerts.available ? VictronAlerts::kSourceBattery : 0) |
        (solarAlerts.available ? VictronAlerts::kSourceSolarCharger : 0);
    next.alertsValid = true;
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
  cerbo_ = new CerboPort(vebusUnitId_);
  LOG_INFO(kTag,
           "Cerbo Modbus TCP monitoring enabled (system ID 100, VE.Bus ID %u)",
           vebusUnitId_);
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
  float capacityAh = capacityAh_;
  portENTER_CRITICAL(&statusMux_);
  const int8_t requestedMode = pendingInverterMode_;
  pendingInverterMode_ = -1;
  portEXIT_CRITICAL(&statusMux_);
  if (requestedMode >= 0) {
    if (!cerbo_->writeInverterMode(static_cast<uint8_t>(requestedMode))) {
      portENTER_CRITICAL(&statusMux_);
      modeRequest_.failed(static_cast<uint8_t>(requestedMode));
      portEXIT_CRITICAL(&statusMux_);
      LOG_WARN(kTag, "Failed to set VE.Bus inverter mode");
    } else {
      LOG_INFO(kTag, "VE.Bus inverter mode set to %d", requestedMode);
    }
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
    portENTER_CRITICAL(&statusMux_);
    cerboTime_ = cerbo_->time;
    cerboTimeMs_ = cerbo_->timeMs;
    portEXIT_CRITICAL(&statusMux_);
    // Prefer the capacity set in the battery monitor over the local setting.
    if (cerbo_->monitorCapacityAh > 0.0F) capacityAh = cerbo_->monitorCapacityAh;
  } else {
    next.shuntValid = next.solarValid = next.dcDcValid = next.valid = false;
    next.shoreValid = false;
    next.shorePower = 0;
    next.shoreState = 255;
    next.solarChargerState = next.dcDcChargerState = 255;
    // Keep the last alert values; the touchscreen ignores them until the
    // Cerbo answers again instead of treating the outage as cleared alerts.
    next.alertsValid = false;
  }
  next.remainingAh = constrain(capacityAh + next.consumedAh, 0.0F, capacityAh);
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
  modeRequest_.observe(status.inverterMode, status.inverterValid);
  portEXIT_CRITICAL(&statusMux_);
}

uint32_t BatteryManager::cerboTime() const {
  portENTER_CRITICAL(&statusMux_);
  const uint32_t time = cerboTime_, timeMs = cerboTimeMs_;
  portEXIT_CRITICAL(&statusMux_);
  const uint32_t age = millis() - timeMs;
  if (time == 0 || age > kTimeValidMs) return 0;
  return time + age / 1000U;
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
  return requestInverterMode(true, enabled);
}

bool BatteryManager::setChargerEnabled(bool enabled) {
  return requestInverterMode(false, enabled);
}

bool BatteryManager::requestInverterMode(bool inverter, bool enabled) {
  if (cerbo_ == nullptr) return false;
  portENTER_CRITICAL(&statusMux_);
  const uint8_t reported = status_.inverterMode;
  const uint8_t current = modeRequest_.base(reported, millis());
  const uint8_t target = modeRequest_.request(inverter, enabled, reported, millis());
  pendingInverterMode_ = static_cast<int8_t>(target);
  portEXIT_CRITICAL(&statusMux_);
  LOG_INFO(kTag, "%s %s requested; current mode=%u, target mode=%u",
           inverter ? "Inverter" : "Charger", enabled ? "on" : "off", current, target);
  return true;
}
