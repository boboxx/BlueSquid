#pragma once
#include <stdint.h>

// An accepted command names the first snapshot containing its result. Do not
// let an older queued notification undo the requested state after its ACK.
struct SnapshotRevisionGate {
  bool waiting = false;
  bool valid = false;
  uint32_t minimum = 0;
  void expect(uint32_t revision) { minimum = revision; waiting = valid = true; }
  bool accepts(uint32_t revision) {
    if (valid && static_cast<int32_t>(revision - minimum) < 0) return false;
    waiting = false;
    return true;
  }
};
