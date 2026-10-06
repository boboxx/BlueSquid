#pragma once
#include <stdint.h>

namespace DisplaySchedule {
inline bool overnight(bool enabled, bool clockValid, unsigned minute,
                      unsigned off, unsigned on) {
  if (!enabled || !clockValid || off == on) return false;
  return off < on ? minute >= off && minute < on : minute >= off || minute < on;
}
struct State {
  bool wasOvernight = false;
  uint32_t wokeAt = 0;
  enum Action { None, Sleep, Wake };
  Action update(bool night, bool sleeping, bool touched, uint32_t now,
                uint32_t lastActivity, uint32_t idleMs, uint32_t nightWakeMs) {
    const bool entered = night && !wasOvernight;
    const bool ended = !night && wasOvernight;
    wasOvernight = night;
    if (touched) { wokeAt = now; return sleeping ? Wake : None; }
    if (ended) return sleeping ? Wake : None;
    if (sleeping) return None;
    if (entered || (night && uint32_t(now - wokeAt) >= nightWakeMs) ||
        (!night && idleMs && uint32_t(now - lastActivity) >= idleMs)) return Sleep;
    return None;
  }
};
}
