#pragma once
#include <stdint.h>

// Called only by the main task. The connection worker and BLE callbacks supply
// an atomic progress counter, avoiding cross-task timestamp ordering races.
class BleReconnectWatchdog {
 public:
  enum class Action { None, Disconnect, Restart };
  void start(uint32_t now, uint32_t progress) {
    lastProgressMs_ = now;
    progress_ = progress;
    disconnectRequested_ = false;
  }
  Action poll(uint32_t now, bool running, uint32_t progress) {
    if (!running) return Action::None;
    // Once cancellation begins, allow 30 seconds for the worker to unwind.
    // Late notifications must not indefinitely postpone a failed cancellation.
    if (disconnectRequested_)
      return now - disconnectMs_ >= 30000 ? Action::Restart : Action::None;
    if (progress != progress_) {
      progress_ = progress;
      lastProgressMs_ = now;
    }
    if (now - lastProgressMs_ >= 30000) {
      disconnectRequested_ = true;
      disconnectMs_ = now;
      return Action::Disconnect;
    }
    return Action::None;
  }
 private:
  uint32_t lastProgressMs_ = 0, progress_ = 0, disconnectMs_ = 0;
  bool disconnectRequested_ = false;
};
