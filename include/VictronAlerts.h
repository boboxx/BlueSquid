#pragma once

#include <stdint.h>
#include <string.h>

// Victron alerts read by the Controller from the Cerbo GX over Modbus TCP and
// forwarded to the touchscreen on an optional characteristic of the
// touchscreen BLE service. Older touchscreens do not look for it, so the
// 111-byte snapshot is unchanged.
namespace VictronAlerts {

constexpr char kUuid[] = "7D8B2004-8A75-4E41-9A6A-35D0A7A18B01";
constexpr uint8_t kPayloadVersion = 1;
constexpr uint8_t kPayloadSize = 13;

// Bit positions in the warning and alarm masks. The values are part of the
// BLE payload and the SD log; append new alerts, never renumber.
enum Id : uint8_t {
  // com.victronenergy.vebus /Alarms/* (registers 34-36, 42, 43, 47).
  kVebusHighTemperature = 0,
  kVebusLowBattery,
  kVebusOverload,
  kVebusTemperatureSensor,
  kVebusVoltageSensor,
  kVebusRipple,
  // com.victronenergy.battery /Alarms/* (registers 268-279).
  kBatteryLowVoltage,
  kBatteryHighVoltage,
  kBatteryLowStarterVoltage,
  kBatteryHighStarterVoltage,
  kBatteryLowSoc,
  kBatteryLowTemperature,
  kBatteryHighTemperature,
  kBatteryMidVoltage,
  kBatteryLowFusedVoltage,
  kBatteryHighFusedVoltage,
  kBatteryFuseBlown,
  kBatteryHighInternalTemperature,
  // com.victronenergy.battery /Alarms/* (registers 320-326).
  kBatteryHighChargeCurrent,
  kBatteryHighDischargeCurrent,
  kBatteryCellImbalance,
  kBatteryInternalFailure,
  kBatteryHighChargeTemperature,
  kBatteryLowChargeTemperature,
  kBatteryLowCellVoltage,
  kLevelAlertCount,
  // Error codes carried as separate payload bytes, not mask bits.
  kVebusError = kLevelAlertCount,
  kSolarChargerError,
  kAlertCount,
};

constexpr uint8_t kFirstBatteryAlert = kBatteryLowVoltage;

enum Level : uint8_t { kNone = 0, kWarning = 1, kAlarm = 2 };

enum SourceFlag : uint8_t {
  kSourceVebus = 1 << 0,
  kSourceBattery = 1 << 1,
  kSourceSolarCharger = 1 << 2,
};

struct Snapshot {
  bool valid = false;  // false: Controller has no current Cerbo data.
  uint8_t sources = 0;
  uint32_t warnings = 0;
  uint32_t alarms = 0;
  uint8_t vebusError = 0;
  uint8_t solarError = 0;

  uint8_t level(uint8_t id) const {
    if (id == kVebusError) return vebusError ? kAlarm : kNone;
    if (id == kSolarChargerError) return solarError ? kAlarm : kNone;
    if (id >= kLevelAlertCount) return kNone;
    if (alarms & (1UL << id)) return kAlarm;
    return (warnings & (1UL << id)) ? kWarning : kNone;
  }
  uint8_t code(uint8_t id) const {
    return id == kVebusError ? vebusError
         : id == kSolarChargerError ? solarError : 0;
  }
};

// Victron reports each alarm register as 0 OK, 1 warning or 2 alarm.
inline void applyRegister(uint32_t& warnings, uint32_t& alarms, uint8_t id,
                          uint16_t value) {
  if (value == 1) warnings |= 1UL << id;
  else if (value == 2) alarms |= 1UL << id;
}

inline uint8_t clampCode(uint16_t value) {
  return value == 0xFFFF ? 0 : value > 255 ? 255 : static_cast<uint8_t>(value);
}

inline void encode(const Snapshot& snapshot, uint8_t* out) {
  out[0] = kPayloadVersion;
  out[1] = snapshot.valid ? 1 : 0;
  out[2] = snapshot.sources;
  for (uint8_t i = 0; i < 4; ++i) {
    out[3 + i] = static_cast<uint8_t>(snapshot.warnings >> (8U * i));
    out[7 + i] = static_cast<uint8_t>(snapshot.alarms >> (8U * i));
  }
  out[11] = snapshot.vebusError;
  out[12] = snapshot.solarError;
}

inline bool decode(const uint8_t* data, size_t length, Snapshot& snapshot) {
  if (data == nullptr || length < kPayloadSize || data[0] != kPayloadVersion)
    return false;
  Snapshot next;
  next.valid = data[1] != 0;
  next.sources = data[2];
  for (uint8_t i = 0; i < 4; ++i) {
    next.warnings |= static_cast<uint32_t>(data[3 + i]) << (8U * i);
    next.alarms |= static_cast<uint32_t>(data[7 + i]) << (8U * i);
  }
  next.vebusError = data[11];
  next.solarError = data[12];
  snapshot = next;
  return true;
}

inline const char* sourceName(uint8_t id) {
  if (id < kFirstBatteryAlert || id == kVebusError) return "Inverter/charger";
  if (id == kSolarChargerError) return "Solar charger";
  return id < kLevelAlertCount ? "Battery monitor" : "Victron";
}

inline const char* alertName(uint8_t id) {
  static const char* const names[kAlertCount] = {
      "High temperature", "Low battery", "Overload",
      "Temperature sensor", "Voltage sensor", "High DC ripple",
      "Low voltage", "High voltage", "Low starter voltage",
      "High starter voltage", "Low state of charge", "Low temperature",
      "High temperature", "Midpoint voltage", "Low fused voltage",
      "High fused voltage", "Fuse blown", "High internal temperature",
      "High charge current", "High discharge current", "Cell imbalance",
      "Internal failure", "High charge temperature",
      "Low charge temperature", "Low cell voltage",
      "VE.Bus error", "Charger error"};
  return id < kAlertCount ? names[id] : "Unknown alert";
}

// Descriptions from Victron's VE.Bus error code list.
inline const char* vebusErrorName(uint8_t code) {
  switch (code) {
    case 1: return "Another phase switched off";
    case 2: return "Mixed device types";
    case 3: return "Not all expected devices found";
    case 4: return "No other device detected";
    case 5: return "AC output overvoltage";
    case 6: return "DDC program error";
    case 7: return "BMS without assistant";
    case 10: return "System time sync problem";
    case 14: return "Device cannot transmit data";
    case 16: return "Dongle missing";
    case 17: return "Master failed; another device took over";
    case 18: return "AC overvoltage on slave output";
    case 22: return "Device cannot be a slave";
    case 24: return "Switch-over protection";
    case 25: return "Firmware incompatibility";
    case 26: return "Internal error";
    default: return "";
  }
}

// Descriptions from Victron's MPPT error code list.
inline const char* solarErrorName(uint8_t code) {
  switch (code) {
    case 1: return "Battery temperature too high";
    case 2: return "Battery voltage too high";
    case 3: case 4: case 5: return "Remote temperature sensor failure";
    case 6: case 7: case 8: return "Remote voltage sense failure";
    case 11: return "Battery high ripple voltage";
    case 14: return "Battery temperature too low";
    case 17: return "Charger temperature too high";
    case 18: return "Charger over-current";
    case 19: return "Charger current reversed";
    case 20: return "Bulk time limit exceeded";
    case 21: return "Current sensor issue";
    case 26: return "Terminals overheated";
    case 28: return "Power stage issue";
    case 33: return "PV voltage too high";
    case 34: return "PV current too high";
    case 38: case 39: return "Input shutdown: battery voltage";
    case 40: return "Input shutdown";
    case 65: return "Communication warning";
    case 66: return "Incompatible device";
    case 67: return "BMS connection lost";
    case 68: return "Network misconfigured";
    case 114: return "CPU temperature too high";
    case 116: return "Calibration data lost";
    case 117: return "Incompatible firmware";
    case 119: return "Settings data lost";
    default: return "";
  }
}

inline const char* codeName(uint8_t id, uint8_t code) {
  return id == kVebusError ? vebusErrorName(code)
       : id == kSolarChargerError ? solarErrorName(code) : "";
}

// Touchscreen alert history. Victron conditions clear themselves at the
// source; acknowledgement only silences the header bell until a new alert is
// raised, and Clear removes alerts that are no longer active.
class Tracker {
 public:
  static constexpr uint8_t kCapacity = 32;
  static constexpr uint8_t kTimeLength = 20;

  struct Entry {
    uint8_t id = 0;
    uint8_t level = kNone;
    uint8_t code = 0;
    bool active = false;
    bool acknowledged = false;
    char raised[kTimeLength]{};
    char cleared[kTimeLength]{};
  };

  enum EventKind : uint8_t { kRaised, kCleared };
  struct Event {
    EventKind kind = kRaised;
    uint8_t id = 0;
    uint8_t level = kNone;
    uint8_t code = 0;
  };

  // Compares a valid snapshot with the active entries. Returns the number of
  // events written; at most kAlertCount * 2 can occur in one update.
  uint8_t update(const Snapshot& snapshot, const char* timestamp,
                 Event* events, uint8_t maxEvents) {
    if (!snapshot.valid) return 0;
    uint8_t count = 0;
    for (uint8_t id = 0; id < kAlertCount; ++id) {
      const uint8_t level = snapshot.level(id);
      const uint8_t code = snapshot.code(id);
      Entry* current = activeEntry(id);
      if (current != nullptr &&
          (current->level != level || current->code != code)) {
        current->active = false;
        copyTime(current->cleared, timestamp);
        addEvent(events, maxEvents, count, kCleared, *current);
        current = nullptr;
        ++revision_;
      }
      if (current == nullptr && level != kNone) {
        Entry& added = append();
        added.id = id;
        added.level = level;
        added.code = code;
        added.active = true;
        added.acknowledged = false;
        copyTime(added.raised, timestamp);
        added.cleared[0] = '\0';
        addEvent(events, maxEvents, count, kRaised, added);
        ++revision_;
      }
    }
    return count;
  }

  uint8_t acknowledgeAll() {
    uint8_t changed = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      if (!entries_[i].acknowledged) ++changed;
      entries_[i].acknowledged = true;
    }
    if (changed) ++revision_;
    return changed;
  }

  // Removes inactive entries and acknowledges the active ones.
  uint8_t clearInactive() {
    uint8_t kept = 0, removed = 0;
    for (uint8_t i = 0; i < count_; ++i) {
      if (!entries_[i].active) { ++removed; continue; }
      entries_[i].acknowledged = true;
      entries_[kept++] = entries_[i];
    }
    count_ = kept;
    ++revision_;
    return removed;
  }

  uint8_t count() const { return count_; }
  // Index 0 is the newest entry.
  const Entry& entry(uint8_t index) const {
    return entries_[count_ - 1 - index];
  }
  uint8_t activeCount() const {
    uint8_t active = 0;
    for (uint8_t i = 0; i < count_; ++i) active += entries_[i].active ? 1 : 0;
    return active;
  }
  // Highest level among unacknowledged entries that are still active.
  uint8_t unacknowledgedLevel() const {
    uint8_t level = kNone;
    for (uint8_t i = 0; i < count_; ++i)
      if (entries_[i].active && !entries_[i].acknowledged &&
          entries_[i].level > level)
        level = entries_[i].level;
    return level;
  }
  uint32_t revision() const { return revision_; }

 private:
  Entry* activeEntry(uint8_t id) {
    for (uint8_t i = 0; i < count_; ++i)
      if (entries_[i].active && entries_[i].id == id) return &entries_[i];
    return nullptr;
  }

  // Makes room by dropping the oldest inactive entry, else the oldest one.
  Entry& append() {
    if (count_ == kCapacity) {
      uint8_t drop = 0;
      for (uint8_t i = 0; i < count_; ++i)
        if (!entries_[i].active) { drop = i; break; }
      for (uint8_t i = drop; i + 1 < count_; ++i) entries_[i] = entries_[i + 1];
      --count_;
    }
    entries_[count_] = Entry();
    return entries_[count_++];
  }

  static void copyTime(char* destination, const char* timestamp) {
    strncpy(destination, timestamp != nullptr ? timestamp : "",
            kTimeLength - 1);
    destination[kTimeLength - 1] = '\0';
  }

  static void addEvent(Event* events, uint8_t maxEvents, uint8_t& count,
                       EventKind kind, const Entry& entry) {
    if (events == nullptr || count >= maxEvents) return;
    events[count].kind = kind;
    events[count].id = entry.id;
    events[count].level = entry.level;
    events[count].code = entry.code;
    ++count;
  }

  Entry entries_[kCapacity];
  uint8_t count_ = 0;
  uint32_t revision_ = 0;
};

}  // namespace VictronAlerts
