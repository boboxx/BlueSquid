#include "TouchHistory.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace TouchHistory {
namespace {

// Longest credible gap between samples; longer gaps count as missing data.
constexpr uint32_t kMaximumSampleGapMs = 5000;

uint16_t deciWattHours(double wattHours) {
  const double value = round(wattHours * 10.0);
  return value <= 0.0 ? 0 : value >= 65535.0 ? 65535 : uint16_t(value);
}

void monthPath(uint32_t time, char* out, size_t size) {
  const time_t value = time;
  struct tm utc{};
  gmtime_r(&value, &utc);
  snprintf(out, size, "%s/%04d-%02d.bin", kDirectory, utc.tm_year + 1900,
           utc.tm_mon + 1);
}

bool ensureDirectory(fs::FS& fs) {
  if (fs.exists(kDirectory)) return true;
  if (!fs.exists("/bluesquid") && !fs.mkdir("/bluesquid")) return false;
  return fs.mkdir(kDirectory);
}

bool historyFileName(const char* name) {
  // "YYYY-MM.bin", possibly with a leading path.
  const char* base = strrchr(name, '/');
  base = base == nullptr ? name : base + 1;
  return strlen(base) == 11 && base[4] == '-' && strcmp(base + 7, ".bin") == 0;
}

time_t localMidnight(time_t value, int dayOffset) {
  struct tm local{};
  localtime_r(&value, &local);
  local.tm_hour = local.tm_min = local.tm_sec = 0;
  local.tm_mday += dayOffset;
  local.tm_isdst = -1;
  return mktime(&local);
}

time_t localHour(time_t midnight, int hour) {
  struct tm local{};
  localtime_r(&midnight, &local);
  local.tm_hour = hour;
  local.tm_isdst = -1;
  return mktime(&local);
}

}  // namespace

void Recorder::sample(uint32_t nowMs, uint32_t time, bool valid,
                      float generatedW, float consumedW, float soc) {
  if (time == 0) {
    hasLast_ = false;
    return;
  }
  const uint32_t slot = time - time % kSlotSeconds;
  if (hasLast_) {
    const uint32_t elapsed = nowMs - lastMs_;
    if (lastValid_ && elapsed <= kMaximumSampleGapMs) {
      // Hold the previous reading over the interval since it was taken.
      generatedWh_ += generatedW_ * elapsed / 3600000.0;
      consumedWh_ += consumedW_ * elapsed / 3600000.0;
      validMs_ += elapsed;
    }
  }
  if (slot != slot_) {
    if (slot_ != 0) finishSlot();
    slot_ = slot;
    generatedWh_ = consumedWh_ = 0.0;
    validMs_ = 0;
    soc_ = kUnknownSoc;
  }
  hasLast_ = true;
  lastMs_ = nowMs;
  lastValid_ = valid;
  generatedW_ = valid ? fmaxf(0.0F, generatedW) : 0.0F;
  consumedW_ = valid ? fmaxf(0.0F, consumedW) : 0.0F;
  if (valid && soc >= 0.0F && soc <= 100.0F) soc_ = uint8_t(lroundf(soc));
}

void Recorder::finishSlot() {
  if (validMs_ == 0) return;  // nothing was measured in this slot
  Record record{};
  record.time = slot_;
  record.generated = deciWattHours(generatedWh_);
  record.consumed = deciWattHours(consumedWh_);
  record.soc = soc_;
  const uint32_t coverage = validMs_ / (kSlotSeconds * 10U);
  record.coverage = coverage > 100 ? 100 : uint8_t(coverage);
  if (count_ == kQueueLimit) {
    pop();
    ++dropped_;
  }
  queue_[(head_ + count_) % kQueueLimit] = record;
  ++count_;
}

void Recorder::pop() {
  if (count_ == 0) return;
  head_ = (head_ + 1) % kQueueLimit;
  --count_;
}

bool append(fs::FS& fs, const Record& record, bool& createdFile) {
  createdFile = false;
  if (!ensureDirectory(fs)) return false;
  char path[40];
  monthPath(record.time, path, sizeof(path));
  createdFile = !fs.exists(path);
  File file = fs.open(path, FILE_APPEND);
  if (!file) return false;
  const size_t written =
      file.write(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  file.close();
  return written == sizeof(record);
}

size_t enforceLimit(fs::FS& fs) {
  for (;;) {
    File directory = fs.open(kDirectory);
    if (!directory || !directory.isDirectory()) return 0;
    size_t total = 0, files = 0;
    String oldest;
    for (File file = directory.openNextFile(); file;
         file = directory.openNextFile()) {
      const String name = file.path();
      if (!file.isDirectory() && historyFileName(name.c_str())) {
        total += file.size();
        ++files;
        if (oldest.isEmpty() || name < oldest) oldest = name;
      }
      file.close();
    }
    directory.close();
    // Never delete the only remaining (current) file.
    if (total <= kStorageLimitBytes || files <= 1 || !fs.remove(oldest))
      return total;
  }
}

void layout(Range range, int offset, time_t now, Chart& chart) {
  chart = Chart();
  chart.range = range;
  const int days = range == Range::Day ? 1 : range == Range::Week ? 7 : 30;
  // The period ends at the midnight after today, shifted back by offset.
  const time_t end = localMidnight(now, 1 - offset * days);
  const time_t start = localMidnight(end, -days);
  chart.start = start;
  chart.end = end;
  if (range == Range::Day) {
    chart.bars = 24;
    for (uint8_t hour = 0; hour < 24; ++hour)
      chart.barStart[hour] = localHour(start, hour);
    chart.points = 288;  // 5-minute SoC points
  } else {
    chart.bars = days;
    for (uint8_t day = 0; day < days; ++day)
      chart.barStart[day] = localMidnight(start, day);
    chart.points = range == Range::Week ? 168 : 180;  // hourly / 4-hourly
  }
  chart.barStart[chart.bars] = end;
  memset(chart.soc, kUnknownSoc, sizeof(chart.soc));
}

bool load(fs::FS& fs, Range range, int offset, time_t now, Chart& chart) {
  layout(range, offset, now, chart);
  if (!fs.exists(kDirectory)) return true;  // nothing recorded yet
  char firstPath[40], lastPath[40];
  monthPath(uint32_t(chart.start), firstPath, sizeof(firstPath));
  monthPath(uint32_t(chart.end - 1), lastPath, sizeof(lastPath));
  const double span = double(chart.end - chart.start);
  // At most two months are spanned by a 30-day period.
  for (const char* path : {firstPath, lastPath}) {
    if (path == lastPath && strcmp(firstPath, lastPath) == 0) break;
    File file = fs.open(path, FILE_READ);
    if (!file) continue;
    Record buffer[64];
    uint8_t bar = 0;
    for (;;) {
      const size_t bytes =
          file.read(reinterpret_cast<uint8_t*>(buffer), sizeof(buffer));
      const size_t count = bytes / sizeof(Record);
      if (count == 0) break;
      for (size_t index = 0; index < count; ++index) {
        const Record& record = buffer[index];
        const time_t time = record.time;
        if (time < chart.start || time >= chart.end) continue;
        if (time < chart.barStart[bar]) bar = 0;
        while (bar + 1 < chart.bars && time >= chart.barStart[bar + 1]) ++bar;
        const float generated = record.generated / 10.0F;
        const float consumed = record.consumed / 10.0F;
        chart.generatedWh[bar] += generated;
        chart.consumedWh[bar] += consumed;
        chart.totalGeneratedWh += generated;
        chart.totalConsumedWh += consumed;
        ++chart.records;
        if (record.soc != kUnknownSoc) {
          const uint16_t point = uint16_t(
              double(time - chart.start) * chart.points / span);
          if (point < chart.points) chart.soc[point] = record.soc;
        }
      }
    }
    file.close();
  }
  return true;
}

}  // namespace TouchHistory
