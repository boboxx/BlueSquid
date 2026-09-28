#pragma once
#include <stdint.h>

// Keep a requested value visible while asynchronous device feedback catches up.
// A newer request replaces the old one; failed sends must not call begin().
template <typename T> struct PendingControl {
  bool waiting = false;
  T desired{};
  uint32_t startedMs = 0;
  void begin(T value, uint32_t now) {
    waiting = true; desired = value; startedMs = now;
  }
  T display(T reported, uint32_t now, uint32_t timeout) {
    if (!waiting) return reported;
    if (reported == desired || uint32_t(now - startedMs) >= timeout) {
      waiting = false;
      return reported;
    }
    return desired;
  }
};
