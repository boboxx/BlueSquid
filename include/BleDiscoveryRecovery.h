#pragma once
#include <stdint.h>
#include <atomic>

class BleDiscoveryRecovery {
 public:
  enum class Action { None, RetryAddress, Restart };
  // The worker can reconnect and lose the link while main-task polling is
  // deferred by GATT maintenance. Latch success so the old outage cannot survive.
  void connectionCompleted() { recovered_.store(true); }
  Action poll(uint32_t now, bool online, bool busy, bool knownAddress, bool restartAllowed) {
    if (recovered_.exchange(false) || online) outage_ = false;
    if (online) return Action::None;
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
  std::atomic_bool recovered_{false};
  bool outage_ = false;
  uint32_t since_ = 0, lastRetry_ = 0;
};
