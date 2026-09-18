#pragma once
#include <stdint.h>

// Only watches an active connection job, never an absent/offline Controller.
class BleReconnectWatchdog {
 public:
  enum class Action { None, Disconnect, Restart };
  void start(uint32_t now) { startedMs_ = now; disconnectRequested_ = false; }
  Action poll(uint32_t now, bool running) {
    if (!running) return Action::None;
    const uint32_t elapsed = now - startedMs_;
    if (elapsed >= 60000) return Action::Restart;
    if (elapsed >= 30000 && !disconnectRequested_) {
      disconnectRequested_ = true;
      return Action::Disconnect;
    }
    return Action::None;
  }
 private:
  uint32_t startedMs_ = 0;
  bool disconnectRequested_ = false;
};
