# Light on/off response review — 2026-09-13

Working baseline: archives/working-backup-20260913-082010/BlueSquid_v1-full.zip.
Full project ZIP verified with CRC and SHA256. Includes current compiled firmware
and dependencies. Does not include settings stored only in physical device NVS.
No firmware edits were made during this review.

## Findings from current code

- RgbwBleDriverManager applies a 40 ms coalescing window and a 100 ms send
  interval to all changes, including discrete on/off commands.
- Sp630eBleAdapter checks work every 25 ms and leaves 100 ms after a write
  batch before the next. A pending status query can hold output writes for up
  to 500 ms. These are separate timing constraints, not a fixed total delay.
- WarmCommandCache::committed replaces the entire cache with an off command.
  The next on command consequently sends coexistence, mode, RGB, CCT, white
  brightness and power again. Five inter-packet delays total 150 ms, before
  GATT write completion time. Retaining settings across an ordinary power-off
  could avoid most of this work; reconnect must still invalidate the cache.
- SensorManager reads temperature and humidity synchronously. The installed
  HTU21DF driver waits 50 ms for each measurement. Main-loop command processing
  can be held up for approximately 100 ms when these reads run.
- SP630E connection parameters request 45–75 ms intervals; the peripheral's
  negotiated timing and its own on/off effects also influence physical response.

## Recommended order

1. Preserve cached non-power registers across ordinary off/on, and invalidate
   them on reconnect or mismatched feedback. Test changed colour while off,
   external mode/settings changes, disconnect/reconnect and failed writes.
2. Give discrete on/off transitions prompt queue service while retaining
   coalescing for drags. Do not starve status queries or bypass availability.
3. Move climate measurement waits out of the command-processing loop.
4. Evaluate shorter BLE intervals only after measuring these changes with both
   SP630Es and the touchscreen connected. Keep the current known-working
   intervals as a fallback.

Use existing per-write and batch-duration logs to measure BLE completion.
Separately measure tap-to-visible-change: a fast acknowledged write does not
prove the peripheral has finished its transition. Compare an idle tap, rapid
on/off, sensor sampling, and sustained colour dragging. Validate each change
independently on hardware before combining it with the next one.
# Applied timing changes (2026-09-14)

Follow-up hardware logs measured unchanged turn-on at 705–741 ms for six
writes, versus 106–111 ms for power-off. The negotiated interval was 40 ms.
The cache now retains acknowledged setup registers across a successful
power-only off command. An unchanged subsequent on can use a single power
write. Accepted status still invalidates differing registers; disconnects and
write failures still discard the cache. Changed settings are written before
power-on. Native regressions cover off-state feedback, external changes,
repeated off, different colour on restoration, and unknown/reset state.
Physical response and peripheral register retention require hardware testing.

SP630E connections now request 15–30 ms intervals (previously 45–75 ms), with
the existing supervision timeout and peripheral parameter acceptance retained.
Actual interval and latency are logged with each completed output batch.

RGB+CCT batches retain acknowledged writes and their existing order/cache and
status reconciliation. The 30 ms inter-command delay now measures from the
previous write's start, including time spent awaiting its acknowledgement.
For acknowledgements taking at least 30 ms this removes five additional pauses
from a six-write batch (150 ms). It does not combine protocol packets.

`command revision=... queue=... ms writes=... ms interval=... x1.25ms latency=...`
measures from the latest adapter submission to batch start, then batch execution.
It excludes touchscreen transport and the manager's initial coalescing delay;
it is not a measurement of physical light response. Hardware verification is
required, including repeated on/off, RGB/WW independently, and both modules
connected. The follow-up adds only the power-off cache retention described
above; batch interruption and command ordering remain unchanged.
