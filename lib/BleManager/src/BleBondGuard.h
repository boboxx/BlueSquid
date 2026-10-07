#pragma once

#include <NimBLEDevice.h>

#include "nimble/nimble/host/include/host/ble_hs.h"

// Accepts new Bluetooth bonds only while a pairing window is open.
//
// "Just Works" pairing lets any nearby device bond, and NimBLE replaces an
// existing bond when a device pairs again. Bonds present at boot or accepted
// during the window are trusted. An encrypted link whose stored keys differ
// from the trusted copy was paired outside the window: it is rejected and
// the bond store is put back as it was, including any bond NimBLE evicted.
class BleBondGuard {
 public:
  static constexpr uint32_t kWindowMs = 120000;
  // A Controller with no bonds (new, or after a flash erase) accepts its
  // first touchscreen for this long after startup.
  static constexpr uint32_t kFirstPairingWindowMs = 300000;

  void begin();
  void openWindow(uint32_t now, uint32_t lengthMs = kWindowMs);
  bool windowOpen(uint32_t now) const;
  // Call from the main loop after encryption completes; keys are persisted
  // after NimBLE's encryption event, so this cannot run in that callback.
  bool verify(const ble_addr_t& peer, uint32_t now);

 private:
  struct Bond {
    bool used = false;
    bool hasOur = false;
    bool hasPeer = false;
    ble_addr_t address{};
    ble_store_value_sec our{};
    ble_store_value_sec peer{};
  };
  static constexpr int kMaximumBonds = MYNEWT_VAL(BLE_STORE_MAX_BONDS);

  static bool read(const ble_addr_t& address, Bond& bond);
  static bool same(const Bond& left, const Bond& right);
  Bond* find(const ble_addr_t& address);
  void trust(const Bond& bond);
  void restoreStore();

  Bond trusted_[kMaximumBonds];
  bool windowActive_ = false;
  uint32_t windowOpenedMs_ = 0;
  uint32_t windowLengthMs_ = kWindowMs;
};
