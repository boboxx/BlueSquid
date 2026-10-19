#include "PowerHistoryStore.h"

#include <LittleFS.h>

#include "Logging.h"

namespace PowerHistory {
namespace {
constexpr char kTag[] = "History";
// Mounted on the data partition of the 8 MB partition table.
constexpr char kPartition[] = "spiffs";
// recordingSince and importedUntil, kept outside the history directory.
constexpr char kStatePath[] = "/bluesquid/history-state.bin";
// Earliest credible record time (2024-01-01).
constexpr uint32_t kEarliestTime = 1704067200UL;
}  // namespace

bool Store::begin() {
  ready_ = LittleFS.begin(true, "/history", 4, kPartition);
  if (!ready_) {
    LOG_WARN(kTag, "Flash file system unavailable; power history is not recorded");
    return false;
  }
  File file = LittleFS.open(kStatePath, FILE_READ);
  if (file) {
    uint32_t values[2]{};
    if (file.read(reinterpret_cast<uint8_t*>(values), sizeof(values)) == sizeof(values)) {
      state_.recordingSince = values[0];
      state_.importedUntil = values[1];
    }
    file.close();
  }
  state_.limit = kStorageLimitBytes;
  state_.bytes = enforceLimit(LittleFS);
  LOG_INFO(kTag, "Power history: %lu bytes stored, recording since %lu",
           static_cast<unsigned long>(state_.bytes),
           static_cast<unsigned long>(state_.recordingSince));
  return true;
}

void Store::sample(uint32_t nowMs, uint32_t time, bool valid, float generatedW,
                   float consumedW, float soc) {
  if (!ready_) return;
  recorder_.sample(nowMs, time, valid, generatedW, consumedW, soc);
  while (recorder_.pending() != 0) {
    const Record record = recorder_.front();
    if (!write(record)) return;  // retried with the next sample
    recorder_.pop();
    if (state_.recordingSince == 0) {
      state_.recordingSince = record.time;
      saveState();
    }
  }
}

bool Store::chart(Chart& chart) {
  return ready_ && accumulate(LittleFS, chart);
}

uint8_t Store::import(const Record* records, size_t count) {
  if (!ready_) return 0;
  uint8_t accepted = 0;
  for (size_t index = 0; index < count; ++index) {
    const Record& record = records[index];
    if (record.time < kEarliestTime || record.time <= state_.importedUntil ||
        (state_.recordingSince != 0 && record.time >= state_.recordingSince))
      continue;
    if (!write(record)) break;
    state_.importedUntil = record.time;
    ++accepted;
  }
  if (accepted != 0) saveState();
  return accepted;
}

bool Store::write(const Record& record) {
  bool created = false;
  if (!append(LittleFS, record, created)) {
    LOG_WARN(kTag, "Could not write power history");
    return false;
  }
  state_.bytes = created ? enforceLimit(LittleFS) : state_.bytes + sizeof(Record);
  return true;
}

void Store::saveState() {
  const uint32_t values[2] = {state_.recordingSince, state_.importedUntil};
  File file = LittleFS.open(kStatePath, FILE_WRITE);
  if (!file) return;
  file.write(reinterpret_cast<const uint8_t*>(values), sizeof(values));
  file.close();
}

}  // namespace PowerHistory
