#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "BleReconnectWatchdog.h"
#include "BleDiscoveryRecovery.h"
int main() {
  using A = BleReconnectWatchdog::Action;
  BleReconnectWatchdog w;
  w.start(100, 0);
  assert(w.poll(30099, true, 0) == A::None);
  assert(w.poll(30100, true, 0) == A::Disconnect);
  assert(w.poll(30101, true, 0) == A::None);
  assert(w.poll(60099, true, 1) == A::None);
  assert(w.poll(60100, true, 2) == A::Restart); // Late data cannot cancel cancellation.
  assert(w.poll(43200000, false, 2) == A::None); // Offline is not a stalled job.

  // Reproduce the log: a lengthy service discovery advances to subscription,
  // then notifications arrive. Crossing 30/60 seconds total is not a timeout.
  w.start(0, 10);
  assert(w.poll(13600, true, 11) == A::None);
  assert(w.poll(25800, true, 12) == A::None);
  assert(w.poll(30030, true, 13) == A::None);
  for (uint32_t now=31000, progress=14; now<120000; now+=1000, ++progress)
    assert(w.poll(now, true, progress) == A::None);
  // Stage/data progress stops: timeout is relative to its last observation.
  assert(w.poll(148999, true, 102) == A::None);
  assert(w.poll(149000, true, 102) == A::Disconnect);
  assert(w.poll(178999, true, 102) == A::None);
  assert(w.poll(179000, true, 102) == A::Restart);
  assert(w.poll(179001, false, 102) == A::None);

  w.start(200000, 103); // A new attempt resets all deadlines/cancellation state.
  assert(w.poll(229999, true, 103) == A::None);
  assert(w.poll(230000, true, 104) == A::None); // Progress at deadline wins.
  assert(w.poll(260000, true, 104) == A::Disconnect);
  // Timer and progress counter wraparound.
  uint32_t start = UINT32_MAX - 1000;
  w.start(start, UINT32_MAX);
  assert(w.poll(start+100U, true, 0) == A::None);
  assert(w.poll(start+30099U, true, 0) == A::None);
  assert(w.poll(start+30100U, true, 0) == A::Disconnect);
  assert(w.poll(start+60100U, true, 0) == A::Restart);
  using D = BleDiscoveryRecovery::Action;
  BleDiscoveryRecovery d;
  assert(d.poll(0, false, false, false, true) == D::None);
  assert(d.poll(43200000, false, false, false, true) == D::None);
  assert(d.poll(100, true, false, true, true) == D::None);
  assert(d.poll(200, false, false, true, true) == D::None);
  assert(d.poll(15199, false, false, true, true) == D::None);
  assert(d.poll(15200, false, false, true, true) == D::RetryAddress);
  assert(d.poll(30200, false, true, true, true) == D::None);
  assert(d.poll(30201, false, false, true, true) == D::RetryAddress);
  assert(d.poll(120200, false, true, true, true) == D::None);
  assert(d.poll(120201, false, false, true, true) == D::Restart);
  assert(d.poll(120202, false, false, true, false) == D::RetryAddress);
  assert(d.poll(120203, false, false, true, false) == D::None);
  assert(d.poll(130000, true, false, true, true) == D::None);
  assert(d.poll(UINT32_MAX-100, false, false, true, true) == D::None);
  assert(d.poll(uint32_t(UINT32_MAX-100)+15000U, false, false, true, true) == D::RetryAddress);
  puts("BLE watchdog and discovery recovery tests passed");
}
