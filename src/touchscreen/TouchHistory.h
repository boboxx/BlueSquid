#pragma once

#include <FS.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

// Power history on the touchscreen SD card. Energy is accumulated in
// 5-minute slots and appended to one file per UTC month under kDirectory.
// When the files exceed kStorageLimitBytes the oldest month is deleted.
namespace TouchHistory {

constexpr uint32_t kSlotSeconds = 300;
constexpr size_t kStorageLimitBytes = 1024UL * 1024UL;
constexpr char kDirectory[] = "/bluesquid/history";
constexpr uint8_t kUnknownSoc = 255;

#pragma pack(push, 1)
struct Record {
  uint32_t time;       // UTC start of the 5-minute slot
  uint16_t generated;  // 0.1 Wh: solar + DC/DC + AC charger output
  uint16_t consumed;   // 0.1 Wh: generation minus battery charge power
  uint8_t soc;         // percent at the end of the slot; kUnknownSoc if none
  uint8_t coverage;    // percent of the slot with valid Cerbo data
};
#pragma pack(pop)
static_assert(sizeof(Record) == 10, "History records are stored as 10 bytes");

// Integrates power readings into slot records. Completed records wait in a
// small queue until the SD card accepts them.
class Recorder {
 public:
  static constexpr size_t kQueueLimit = 288;  // one day of slots

  // time is UTC seconds, or 0 while the clock is unknown.
  void sample(uint32_t nowMs, uint32_t time, bool valid, float generatedW,
              float consumedW, float soc);
  size_t pending() const { return count_; }
  const Record& front() const { return queue_[head_]; }
  void pop();
  uint32_t dropped() const { return dropped_; }

 private:
  void finishSlot();

  uint32_t slot_ = 0;
  uint32_t lastMs_ = 0;
  bool hasLast_ = false;
  bool lastValid_ = false;
  float generatedW_ = 0.0F, consumedW_ = 0.0F;
  double generatedWh_ = 0.0, consumedWh_ = 0.0;
  uint32_t validMs_ = 0;
  uint8_t soc_ = kUnknownSoc;
  Record queue_[kQueueLimit]{};
  size_t head_ = 0, count_ = 0;
  uint32_t dropped_ = 0;
};

// Appends one record. Returns false if the directory or file cannot be
// written. createdFile is set when a new monthly file was started.
bool append(fs::FS& fs, const Record& record, bool& createdFile);
// Deletes the oldest monthly files until the total fits the limit.
// Returns the bytes still in use.
size_t enforceLimit(fs::FS& fs);

enum class Range : uint8_t { Day, Week, Month };

struct Chart {
  static constexpr uint8_t kMaxBars = 31;
  static constexpr uint16_t kMaxPoints = 288;
  Range range = Range::Day;
  uint8_t bars = 0;
  uint16_t points = 0;
  time_t start = 0, end = 0;  // local period [start, end)
  time_t barStart[kMaxBars + 1]{};
  float generatedWh[kMaxBars]{};
  float consumedWh[kMaxBars]{};
  uint8_t soc[kMaxPoints]{};  // kUnknownSoc where no sample exists
  uint32_t records = 0;
  float totalGeneratedWh = 0.0F, totalConsumedWh = 0.0F;
};

// Lays out the period ending `offset` periods before the one containing
// `now` (local time) and fills it from the SD card. Returns false only when
// the history directory cannot be read.
bool load(fs::FS& fs, Range range, int offset, time_t now, Chart& chart);
// Bars and points for a period, without reading any data.
void layout(Range range, int offset, time_t now, Chart& chart);

}  // namespace TouchHistory
