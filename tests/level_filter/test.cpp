#include "LevelFilter.h"
#include <assert.h>
#include <math.h>

int main() {
  LevelFilter pitch;
  pitch.reset(3.5F);
  assert(pitch.report(3.5F) == 0);
  // Alternating stationary noise and isolated ADC spikes must not move zero.
  for (unsigned i = 0; i < 200; ++i) {
    pitch.update(i % 17 == 0 ? 8.0F : 3.5F + (i % 2 ? 0.3F : -0.3F), 0.1F);
    assert(pitch.report(3.5F) == 0);
  }
  // Persistent real movement is not mistaken for noise or recalibrated away.
  for (unsigned i = 0; i < 35; ++i) pitch.update(8.5F, 0.1F);
  assert(fabsf(pitch.report(3.5F) - 5.0F) <= 0.11F);
  // Small sustained changes also eventually appear (within the 0.15 deg band).
  pitch.reset(3.5F);
  pitch.report(3.5F);
  for (unsigned i = 0; i < 30; ++i) pitch.update(3.8F, 0.1F);
  assert(pitch.report(3.5F) >= 0.2F);
  // Hysteresis at a nonzero attitude must behave identically.
  pitch.reset(8.5F);
  assert(pitch.report(3.5F) == 5.0F);
  for (unsigned i = 0; i < 100; ++i) {
    pitch.update(8.5F + (i % 2 ? 0.1F : -0.1F), 0.1F);
    assert(pitch.report(3.5F) == 5.0F);
  }
  // Calibration changes force a fresh reported value, without stale history.
  assert(pitch.report(8.5F, true) == 0);
  assert(!signbit(pitch.report(8.5F)));
  pitch.reset(-2.5F);
  assert(pitch.report(-2.5F) == 0);
  for (unsigned i = 0; i < 40; ++i) pitch.update(-5.5F, 0.1F);
  assert(fabsf(pitch.report(-2.5F) + 3.0F) <= 0.11F);
  // Invalid values never poison future filtering.
  const float before = pitch.update(-5.5F, 0.1F);
  assert(pitch.update(NAN, 0.1F) == before);
}
