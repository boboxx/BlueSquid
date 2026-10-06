#pragma once
#include <math.h>
#include <stdint.h>

// Samples arrive at 10 Hz. Reject isolated spikes, smooth stationary noise,
// then publish tenths with hysteresis. Never adjust the saved level reference.
class LevelFilter {
 public:
  float update(float sample, float alpha) {
    if (!isfinite(sample)) return filtered_;
    if (!initialized_) reset(sample);
    samples_[next_] = sample;
    next_ = (next_ + 1) % 5;
    float sorted[5];
    for (unsigned i = 0; i < 5; ++i) sorted[i] = samples_[i];
    for (unsigned i = 1; i < 5; ++i) {
      const float value = sorted[i];
      unsigned j = i;
      while (j && sorted[j - 1] > value) { sorted[j] = sorted[j - 1]; --j; }
      sorted[j] = value;
    }
    // Drop the largest and smallest sample, then average the remaining three.
    const float robustSample = (sorted[1] + sorted[2] + sorted[3]) / 3.0F;
    const float delta = robustSample - filtered_;
    // Follow a substantial leveling adjustment faster than stationary noise.
    const float weight = fabsf(delta) >= 1.0F ? 0.30F : alpha;
    filtered_ += weight * delta;
    return filtered_;
  }
  void reset(float absolute) {
    filtered_ = absolute;
    for (float& sample : samples_) sample = absolute;
    next_ = 0;
    initialized_ = true;
    reportedInitialized_ = false;
  }
  float report(float zero, bool force = false) {
    const float relative = filtered_ - zero;
    if (force || !reportedInitialized_ || fabsf(relative - reported_) >= 0.15F) {
      reported_ = roundf(relative * 10.0F) / 10.0F;
      // Normalize negative zero for both diagnostics and the display.
      if (reported_ == 0.0F) reported_ = 0.0F;
      reportedInitialized_ = true;
    }
    return reported_;
  }
 private:
  float samples_[5]{};
  float filtered_ = 0.0F, reported_ = 0.0F;
  uint8_t next_ = 0;
  bool initialized_ = false, reportedInitialized_ = false;
};
