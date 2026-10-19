#pragma once

#include "PowerHistory.h"

namespace PowerHistory {

// The Controller's power history: records 5-minute slots in the flash file
// system and answers touchscreen requests. Use from a single task.
class Store {
 public:
  bool begin();
  // time is UTC seconds, or 0 while the Cerbo clock is unknown.
  void sample(uint32_t nowMs, uint32_t time, bool valid, float generatedW,
              float consumedW, float soc);
  // Fills a chart laid out by a touchscreen.
  bool chart(Chart& chart);
  Link::Status status() const { return state_; }
  // Adds records from a touchscreen SD card, in ascending time order. Only
  // records older than the first slot recorded here, and newer than any
  // already imported, are kept, so an interrupted upload can resume.
  uint8_t import(const Record* records, size_t count);

 private:
  bool write(const Record& record);
  void saveState();

  Recorder recorder_;
  Link::Status state_;
  bool ready_ = false;
};

}  // namespace PowerHistory
