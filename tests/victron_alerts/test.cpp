#include "VictronAlerts.h"
#include <cassert>
#include <cstring>

using namespace VictronAlerts;

int main() {
  // Register levels map to the warning and alarm masks.
  uint32_t warnings = 0, alarms = 0;
  applyRegister(warnings, alarms, kBatteryLowSoc, 1);
  applyRegister(warnings, alarms, kVebusOverload, 2);
  applyRegister(warnings, alarms, kBatteryFuseBlown, 0);
  applyRegister(warnings, alarms, kBatteryHighVoltage, 0xFFFF);
  assert(warnings == (1UL << kBatteryLowSoc));
  assert(alarms == (1UL << kVebusOverload));
  assert(clampCode(0xFFFF) == 0 && clampCode(300) == 255 && clampCode(17) == 17);
  static_assert(kLevelAlertCount <= 32, "Alert masks are 32 bits");

  // Payload round trip; short or unknown payloads are rejected.
  Snapshot sent;
  sent.valid = true;
  sent.sources = kSourceVebus | kSourceBattery;
  sent.warnings = warnings;
  sent.alarms = alarms | (1UL << kBatteryLowCellVoltage);
  sent.vebusError = 17;
  sent.solarError = 33;
  uint8_t payload[kPayloadSize]{};
  encode(sent, payload);
  Snapshot received;
  assert(decode(payload, sizeof(payload), received));
  assert(received.valid && received.sources == sent.sources);
  assert(received.warnings == sent.warnings && received.alarms == sent.alarms);
  assert(received.vebusError == 17 && received.solarError == 33);
  assert(received.level(kBatteryLowSoc) == kWarning);
  assert(received.level(kBatteryLowCellVoltage) == kAlarm);
  assert(received.level(kVebusError) == kAlarm && received.code(kVebusError) == 17);
  assert(!decode(payload, kPayloadSize - 1, received));
  payload[0] = 9;
  assert(!decode(payload, sizeof(payload), received));
  assert(std::strcmp(sourceName(kSolarChargerError), "Solar charger") == 0);
  assert(std::strcmp(sourceName(kBatteryLowVoltage), "Battery monitor") == 0);
  assert(std::strcmp(sourceName(kVebusRipple), "Inverter/charger") == 0);
  assert(std::strcmp(alertName(kBatteryLowCellVoltage), "Low cell voltage") == 0);
  assert(std::strcmp(codeName(kSolarChargerError, 33), "PV voltage too high") == 0);

  Tracker tracker;
  Tracker::Event events[kAlertCount * 2];
  // Invalid snapshots (Cerbo offline) neither raise nor clear alerts.
  Snapshot snapshot;
  snapshot.alarms = 1UL << kBatteryLowVoltage;
  assert(tracker.update(snapshot, "t0", events, kAlertCount * 2) == 0);
  assert(tracker.count() == 0);

  snapshot.valid = true;
  assert(tracker.update(snapshot, "2026-10-08 14:32", events, kAlertCount * 2) == 1);
  assert(events[0].kind == Tracker::kRaised && events[0].id == kBatteryLowVoltage);
  assert(tracker.unacknowledgedLevel() == kAlarm && tracker.activeCount() == 1);
  assert(std::strcmp(tracker.entry(0).raised, "2026-10-08 14:32") == 0);
  // An unchanged snapshot produces no events.
  const uint32_t revision = tracker.revision();
  assert(tracker.update(snapshot, "later", events, kAlertCount * 2) == 0);
  assert(tracker.revision() == revision);

  // Acknowledging silences the bell while the alert stays active.
  assert(tracker.acknowledgeAll() == 1);
  assert(tracker.unacknowledgedLevel() == kNone && tracker.activeCount() == 1);

  // A new warning raises the bell again; a level change is a new alert.
  snapshot.warnings = 1UL << kBatteryLowSoc;
  assert(tracker.update(snapshot, "t2", events, kAlertCount * 2) == 1);
  assert(tracker.unacknowledgedLevel() == kWarning);
  snapshot.warnings = 0;
  snapshot.alarms |= 1UL << kBatteryLowSoc;
  assert(tracker.update(snapshot, "t3", events, kAlertCount * 2) == 2);
  assert(events[0].kind == Tracker::kCleared && events[0].level == kWarning);
  assert(events[1].kind == Tracker::kRaised && events[1].level == kAlarm);
  assert(tracker.count() == 3 && tracker.activeCount() == 2);

  // Error code changes are separate alerts.
  snapshot.vebusError = 17;
  assert(tracker.update(snapshot, "t4", events, kAlertCount * 2) == 1);
  snapshot.vebusError = 5;
  assert(tracker.update(snapshot, "t5", events, kAlertCount * 2) == 2);
  assert(tracker.entry(0).code == 5 && tracker.entry(0).active);
  assert(!tracker.entry(1).active && std::strcmp(tracker.entry(1).cleared, "t5") == 0);

  // Conditions clearing at the source mark entries inactive; Clear removes them.
  snapshot.alarms = 0;
  snapshot.vebusError = 0;
  tracker.update(snapshot, "t6", events, kAlertCount * 2);
  assert(tracker.activeCount() == 0 && tracker.unacknowledgedLevel() == kNone);
  assert(tracker.clearInactive() == 5 && tracker.count() == 0);

  // Clear keeps active entries and acknowledges them.
  snapshot.alarms = 1UL << kVebusOverload;
  tracker.update(snapshot, "t7", events, kAlertCount * 2);
  assert(tracker.clearInactive() == 0 && tracker.count() == 1);
  assert(tracker.unacknowledgedLevel() == kNone);

  // A full history drops the oldest inactive entry first.
  Tracker full;
  Snapshot toggling;
  toggling.valid = true;
  for (uint8_t i = 0; i < 80; ++i) {
    toggling.warnings = (i % 2 == 0) ? (1UL << kBatteryLowSoc) : 0;
    toggling.alarms = 1UL << kVebusOverload;
    full.update(toggling, "x", nullptr, 0);
  }
  assert(full.count() == Tracker::kCapacity);
  bool overloadActive = false;
  for (uint8_t i = 0; i < full.count(); ++i)
    overloadActive |= full.entry(i).id == kVebusOverload && full.entry(i).active;
  assert(overloadActive);
  return 0;
}
