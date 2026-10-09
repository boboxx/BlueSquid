#include "TouchHistory.h"

#include <cassert>
#include <cmath>
#include <cstdlib>
#include <ctime>

using namespace TouchHistory;

namespace {

// 2026-10-08 00:00:00 UTC
constexpr uint32_t kDay = 1791417600UL;

void appendAll(fs::FS& fs, Recorder& recorder) {
  while (recorder.pending() != 0) {
    bool created = false;
    assert(append(fs, recorder.front(), created));
    recorder.pop();
  }
}

}  // namespace

int main() {
  setenv("TZ", "UTC0", 1);
  tzset();

  // Constant 600 W generated and 300 W consumed for one full slot.
  Recorder recorder;
  recorder.sample(0, 0, true, 600, 300, 80);  // unknown clock: ignored
  for (uint32_t second = 0; second <= kSlotSeconds; ++second)
    recorder.sample(second * 1000, kDay + second, true, 600, 300, 80);
  assert(recorder.pending() == 1);
  Record record = recorder.front();
  assert(record.time == kDay);
  assert(record.generated == 500);  // 50.0 Wh
  assert(record.consumed == 250);   // 25.0 Wh
  assert(record.soc == 80 && record.coverage == 100);
  recorder.pop();

  // Half the slot invalid; a 10-second gap in sampling is not integrated.
  Recorder partial;
  const uint32_t slot = kDay + kSlotSeconds;
  for (uint32_t second = 0; second < 150; ++second)
    partial.sample(second * 1000, slot + second, second < 140, 3600, 0, 50);
  partial.sample(160000, slot + 160, true, 3600, 0, 51);
  for (uint32_t second = 161; second <= kSlotSeconds; ++second)
    partial.sample(second * 1000, slot + second, false, 3600, 0, 52);
  assert(partial.pending() == 1);
  record = partial.front();
  // 140 s before the gap plus the 1 s after the reading at 160 s.
  assert(record.generated == 1410);
  assert(record.coverage == 47);
  assert(record.soc == 51);  // last valid SoC
  partial.pop();

  // A slot without any valid data produces no record.
  Recorder idle;
  for (uint32_t second = 0; second <= kSlotSeconds; ++second)
    idle.sample(second * 1000, kDay + second, false, 0, 0, 0);
  assert(idle.pending() == 0);

  // Store a day of hourly-varying slots and read it back.
  fs::FS fs;
  Recorder day;
  uint32_t ms = 0;
  for (uint32_t second = 0; second <= 86400; ++second, ms += 1000) {
    const float watts = 120.0F * (second / 3600);  // 0 W, 120 W, ...
    day.sample(ms, kDay + second, true, watts, 60, (second / 3600) % 101);
    if (day.pending() > 200) appendAll(fs, day);
  }
  appendAll(fs, day);
  assert(fs.exists("/bluesquid/history/2026-10.bin"));
  assert(fs.storage.files["/bluesquid/history/2026-10.bin"].size() ==
         288 * sizeof(Record));

  Chart chart;
  const time_t now = kDay + 12 * 3600;
  assert(load(fs, Range::Day, 0, now, chart));
  assert(chart.bars == 24 && chart.points == 288 && chart.records == 288);
  assert(chart.start == time_t(kDay) && chart.end == time_t(kDay + 86400));
  for (uint8_t hour = 0; hour < 24; ++hour) {
    assert(std::fabs(chart.generatedWh[hour] - 120.0F * hour) < 0.5F);
    assert(std::fabs(chart.consumedWh[hour] - 60.0F) < 0.5F);
  }
  assert(chart.soc[0] == 0 && chart.soc[12] == 1 && chart.soc[287] == 23);

  // The previous day and the week view.
  assert(load(fs, Range::Day, 1, now, chart) && chart.records == 0);
  assert(load(fs, Range::Week, 0, now, chart));
  assert(chart.bars == 7 && chart.records == 288);
  assert(chart.barStart[6] == time_t(kDay));
  assert(std::fabs(chart.generatedWh[6] - 120.0F * 276) < 2.0F);
  assert(load(fs, Range::Month, 0, now, chart) && chart.bars == 30);

  // Local day boundaries follow the selected zone (Atlantic, UTC-3 in Oct).
  setenv("TZ", "AST4ADT,M3.2.0,M11.1.0", 1);
  tzset();
  assert(load(fs, Range::Day, 0, now, chart));
  assert(chart.start == time_t(kDay + 3 * 3600));
  assert(chart.records == 288 - 36);  // 21 hours of this UTC day
  setenv("TZ", "UTC0", 1);
  tzset();

  // Storage limit: the oldest months are removed, the newest kept.
  fs::FS full;
  assert(full.mkdir("/bluesquid") && full.mkdir(kDirectory));
  for (int month = 1; month <= 12; ++month) {
    char path[48];
    snprintf(path, sizeof(path), "%s/2025-%02d.bin", kDirectory, month);
    full.storage.files[path].assign(100 * 1024, 0);
  }
  full.storage.files["/bluesquid/history/notes.txt"].assign(500000, 0);
  const size_t used = enforceLimit(full);
  assert(used <= kStorageLimitBytes && used == 10 * 100 * 1024);
  assert(!full.exists("/bluesquid/history/2025-01.bin"));
  assert(!full.exists("/bluesquid/history/2025-02.bin"));
  assert(full.exists("/bluesquid/history/2025-03.bin"));
  assert(full.exists("/bluesquid/history/2025-12.bin"));
  assert(full.exists("/bluesquid/history/notes.txt"));
  return 0;
}
