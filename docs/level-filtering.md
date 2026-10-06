# Pitch and roll stability

Controller 1.0.25 keeps the existing 16 ADC samples per axis and 100 ms sampling
interval. A rolling five-sample window now discards the highest and lowest
angle and averages the other three before exponential smoothing. The smoothing
weight is 0.10 for small changes and 0.30 when the difference is at least 1°.

Reported values are rounded to 0.1° and held until the filtered angle differs
from the last reported value by at least 0.15°. This reduces last-digit flicker
at zero and at nonzero attitudes. It deliberately trades small-change response
for stability; displayed precision is not a claim of sensor accuracy. Slow
sensor drift can still eventually move the reading.

Saved calibration offsets are never adjusted automatically. Calibrating level
resets filter history; restoring calibration immediately recalculates the
reported angles. Serial logs retain ADC millivolts, raw angles, filtered absolute
angles, calibration offsets and reported values to help distinguish noise from
drift or real movement.

`bash tests/level_filter/run.sh` checks representative stationary noise,
isolated spikes, sustained small and large changes, calibration resets,
nonzero attitudes, negative zero and invalid samples. Physical stability and
response time need checking after uploading `main_controller`. The touchscreen
does not need an update for this change.
