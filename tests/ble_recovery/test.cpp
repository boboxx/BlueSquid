#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "BleReconnectWatchdog.h"
int main() {
  using A = BleReconnectWatchdog::Action;
  BleReconnectWatchdog w;
  w.start(100);
  assert(w.poll(30099, true) == A::None);
  assert(w.poll(30100, true) == A::Disconnect);
  assert(w.poll(30101, true) == A::None);
  assert(w.poll(60099, true) == A::None);
  assert(w.poll(60100, true) == A::Restart);
  // A completed/failed job and an absent Controller never cause a reboot.
  assert(w.poll(43200000, false) == A::None);
  w.start(43200000);
  assert(w.poll(43201000, true) == A::None);
  assert(w.poll(43230000, true) == A::Disconnect);
  // Deadlines remain correct across millis() wraparound.
  uint32_t start = UINT32_MAX - 1000;
  w.start(start);
  assert(w.poll(start + 29999U, true) == A::None);
  assert(w.poll(start + 30000U, true) == A::Disconnect);
  assert(w.poll(start + 60000U, true) == A::Restart);
  puts("BLE reconnect watchdog tests passed");
}
