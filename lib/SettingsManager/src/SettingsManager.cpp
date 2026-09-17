#include "SettingsManager.h"

#include "Logging.h"

namespace {
constexpr char kTag[] = "Settings";
constexpr char kNamespace[] = "sensors";
constexpr char kPitchZeroKey[] = "pitchZero";
constexpr char kRollZeroKey[] = "rollZero";
constexpr char kJkBmsAddressesKey[] = "jkBmsAddr";
constexpr char kBatteryCapacityKey[] = "batteryAh";
constexpr char kDeviceStateKey[] = "deviceState";
constexpr char kSp630eAssignmentsKey[] = "sp630eMap";
constexpr char kCerboStationSsidKey[] = "cerboStaSsid";
constexpr char kCerboStationPasswordKey[] = "cerboStaPass";
constexpr char kCerboVebusUnitIdKey[] = "vebusId";
constexpr uint32_t kDeviceStateMagic = 0x42534453UL;  // "BSDS"
constexpr uint16_t kDeviceStateVersion = 1;

struct DeviceStateRecord {
  uint32_t magic = kDeviceStateMagic;
  uint16_t version = kDeviceStateVersion;
  uint16_t payloadSize = sizeof(PersistentDeviceState);
  PersistentDeviceState state{};
  uint32_t checksum = 0;
};

uint32_t checksum(const DeviceStateRecord& record) {
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&record);
  const size_t length = sizeof(record) - sizeof(record.checksum);
  uint32_t value = 2166136261UL;
  for (size_t index = 0; index < length; ++index) {
    value ^= bytes[index];
    value *= 16777619UL;
  }
  return value;
}
}

bool SettingsManager::begin() {
  ready_ = preferences_.begin(kNamespace, false);
  LOG_INFO(kTag, "Persistent settings %s", ready_ ? "ready" : "unavailable");
  return ready_;
}

void SettingsManager::end() {
  if (ready_) {
    preferences_.end();
    ready_ = false;
  }
}

bool SettingsManager::loadLevelCalibration(float& pitchZeroDegrees,
                                           float& rollZeroDegrees) {
  if (!ready_) {
    return false;
  }

  pitchZeroDegrees = preferences_.getFloat(kPitchZeroKey, 0.0F);
  rollZeroDegrees = preferences_.getFloat(kRollZeroKey, 0.0F);
  return true;
}

bool SettingsManager::saveLevelCalibration(float pitchZeroDegrees,
                                           float rollZeroDegrees) {
  if (!ready_) {
    LOG_ERROR(kTag, "Cannot save level calibration: settings unavailable");
    return false;
  }

  const size_t pitchBytes =
      preferences_.putFloat(kPitchZeroKey, pitchZeroDegrees);
  const size_t rollBytes =
      preferences_.putFloat(kRollZeroKey, rollZeroDegrees);
  const bool saved = pitchBytes == sizeof(float) && rollBytes == sizeof(float);
  LOG_INFO(kTag, "Level calibration persistence: %s",
           saved ? "saved" : "failed");
  return saved;
}

float SettingsManager::loadBatteryCapacityAh(float defaultCapacityAh) {
  if (!ready_) return defaultCapacityAh;
  return preferences_.getFloat(kBatteryCapacityKey, defaultCapacityAh);
}

bool SettingsManager::saveBatteryCapacityAh(float capacityAh) {
  if (!ready_) {
    LOG_ERROR(kTag, "Cannot save battery capacity: settings unavailable");
    return false;
  }
  const bool saved =
      preferences_.putFloat(kBatteryCapacityKey, capacityAh) == sizeof(float);
  LOG_INFO(kTag, "Battery capacity %.1f Ah persistence: %s", capacityAh,
           saved ? "saved" : "failed");
  return saved;
}

String SettingsManager::loadJkBmsAddresses(const char* defaultAddresses) {
  if (!ready_) {
    return String(defaultAddresses);
  }
  return preferences_.getString(kJkBmsAddressesKey, defaultAddresses);
}

bool SettingsManager::saveJkBmsAddresses(const String& addresses) {
  if (!ready_) {
    LOG_ERROR(kTag, "Cannot save JK BMS addresses: settings unavailable");
    return false;
  }

  const size_t bytes = preferences_.putString(kJkBmsAddressesKey, addresses);
  const bool saved = bytes == addresses.length();
  LOG_INFO(kTag, "JK BMS address persistence: %s",
           saved ? "saved" : "failed");
  return saved;
}

bool SettingsManager::loadSp630eAssignments(Sp630eAssignment (&assignments)[8]) {
  if (!ready_) return false;
  const size_t size = preferences_.getBytesLength(kSp630eAssignmentsKey);
  uint8_t bytes[sizeof(assignments) + 1]{};
  if (size != sizeof(bytes)) return false;
  if (preferences_.getBytes(kSp630eAssignmentsKey, bytes, size) != size) return false;
  memcpy(assignments, bytes, sizeof(assignments));
  return true;
}
uint8_t SettingsManager::loadLightGroupMask() {
  uint8_t bytes[sizeof(Sp630eAssignment) * 8 + 1]{};
  if (!ready_ || preferences_.getBytesLength(kSp630eAssignmentsKey) != sizeof(bytes) ||
      preferences_.getBytes(kSp630eAssignmentsKey, bytes, sizeof(bytes)) != sizeof(bytes)) return 15;
  return bytes[sizeof(bytes)-1] & 15;
}
bool SettingsManager::saveSp630eConfiguration(const Sp630eAssignment (&assignments)[8], uint8_t group) {
  if (!ready_ || group > 15) return false;
  uint8_t bytes[sizeof(assignments) + 1]{};
  memcpy(bytes, assignments, sizeof(assignments));
  bytes[sizeof(assignments)] = group;
  return preferences_.putBytes(kSp630eAssignmentsKey, bytes, sizeof(bytes)) == sizeof(bytes);
}
bool SettingsManager::saveSp630eAssignments(const Sp630eAssignment (&assignments)[8]) {
  return saveSp630eConfiguration(assignments, loadLightGroupMask());
}

String SettingsManager::loadCerboStationSsid(const char* defaultValue) {
  return ready_ ? preferences_.getString(kCerboStationSsidKey, defaultValue)
                : String(defaultValue);
}

String SettingsManager::loadCerboStationPassword(const char* defaultValue) {
  return ready_ ? preferences_.getString(kCerboStationPasswordKey, defaultValue)
                : String(defaultValue);
}

bool SettingsManager::saveCerboStation(const String& ssid, const String& password) {
  if (!ready_) return false;
  const bool ssidSaved = preferences_.putString(kCerboStationSsidKey, ssid) == ssid.length();
  const bool passwordSaved =
      preferences_.putString(kCerboStationPasswordKey, password) == password.length();
  LOG_INFO(kTag, "Cerbo Wi-Fi client settings persistence: %s",
           ssidSaved && passwordSaved ? "saved" : "failed");
  return ssidSaved && passwordSaved;
}

uint8_t SettingsManager::loadCerboVebusUnitId(uint8_t defaultValue) {
  return ready_ ? preferences_.getUChar(kCerboVebusUnitIdKey, defaultValue)
                : defaultValue;
}

bool SettingsManager::saveCerboVebusUnitId(uint8_t unitId) {
  if (!ready_ || unitId == 0 || unitId > 247) return false;
  const bool saved =
      preferences_.putUChar(kCerboVebusUnitIdKey, unitId) == sizeof(unitId);
  LOG_INFO(kTag, "VE.Bus unit ID %u persistence: %s", unitId,
           saved ? "saved" : "failed");
  return saved;
}

bool SettingsManager::loadDeviceState(PersistentDeviceState& state) {
  if (!ready_ ||
      preferences_.getBytesLength(kDeviceStateKey) !=
          sizeof(DeviceStateRecord)) {
    return false;
  }

  DeviceStateRecord record{};
  if (preferences_.getBytes(kDeviceStateKey, &record, sizeof(record)) !=
      sizeof(record) ||
      record.magic != kDeviceStateMagic ||
      record.version != kDeviceStateVersion ||
      record.payloadSize != sizeof(PersistentDeviceState) ||
      record.checksum != checksum(record)) {
    LOG_WARN(kTag, "Stored device state is invalid; using safe defaults");
    return false;
  }

  state = record.state;
  LOG_INFO(kTag, "Persistent device state loaded");
  return true;
}

bool SettingsManager::saveDeviceState(const PersistentDeviceState& state) {
  if (!ready_) {
    LOG_ERROR(kTag, "Cannot save device state: settings unavailable");
    return false;
  }

  DeviceStateRecord record{};
  record.state = state;
  record.checksum = checksum(record);
  const bool saved =
      preferences_.putBytes(kDeviceStateKey, &record, sizeof(record)) ==
      sizeof(record);
  LOG_INFO(kTag, "Device state persistence: %s",
           saved ? "saved" : "failed");
  return saved;
}

RvcFan::Config SettingsManager::loadRvcFanConfiguration() {
  RvcFan::Config value{};
  if (!ready_ || preferences_.getBytesLength("rvcFan") != sizeof(value)) return value;
  if (preferences_.getBytes("rvcFan", &value, sizeof(value)) != sizeof(value) || !RvcFan::valid(value)) return {};
  return value;
}
bool SettingsManager::saveRvcFanConfiguration(const RvcFan::Config& config) {
  const bool saved = ready_ && RvcFan::valid(config) &&
      preferences_.putBytes("rvcFan", &config, sizeof(config)) == sizeof(config);
  if (saved) ++rvcFanRevision_;
  return saved;
}
