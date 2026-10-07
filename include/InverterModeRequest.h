#pragma once
#include <stdint.h>

// VE.Bus switch position: 1=charger only, 2=inverter only, 3=on, 4=off.
// Inverter and charger are separate buttons sharing one register. Base each
// change on the last requested position until a Cerbo poll confirms it;
// otherwise a quick second tap starts from a stale poll and undoes the first.
struct InverterModeRequest {
  static constexpr uint32_t kConfirmMs = 10000;
  uint8_t mode = 0;  // 0: no unconfirmed request.
  uint32_t startedMs = 0;

  uint8_t base(uint8_t reported, uint32_t now) const {
    return mode && uint32_t(now - startedMs) < kConfirmMs ? mode : reported;
  }
  uint8_t request(bool inverter, bool enabled, uint8_t reported, uint32_t now) {
    const uint8_t current = base(reported, now);
    const bool other = inverter ? (current == 1 || current == 3)
                                : (current == 2 || current == 3);
    mode = inverter ? (enabled ? (other ? 3 : 2) : (other ? 1 : 4))
                    : (enabled ? (other ? 3 : 1) : (other ? 2 : 4));
    startedMs = now;
    return mode;
  }
  void observe(uint8_t reported, bool valid) {
    if (valid && reported == mode) mode = 0;
  }
  void failed(uint8_t written) {
    if (mode == written) mode = 0;
  }
};
