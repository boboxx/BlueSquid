#pragma once
#include <stdint.h>

class BleDiscoveryRecovery {
 public:
  enum class Action { None, RetryAddress, Restart };
  Action poll(uint32_t now, bool online, bool busy, bool knownAddress, bool restartAllowed) {
    if (online) { outage_ = false; return Action::None; }
    if (!knownAddress) return Action::None;
    if (!outage_) { outage_ = true; since_ = lastRetry_ = now; }
    if (busy) return Action::None;
    if (restartAllowed && now - since_ >= 120000) return Action::Restart;
    if (now - lastRetry_ >= 15000) {
      lastRetry_ = now;
      return Action::RetryAddress;
    }
    return Action::None;
  }
 private:
  bool outage_ = false;
  uint32_t since_ = 0, lastRetry_ = 0;
};
