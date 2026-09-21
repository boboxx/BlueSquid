# Touchscreen BLE recovery — 1.0.24

The reported overnight outage showed the Controller advertising with no connected
clients. Both submitted logs were from the Controller port. The touchscreen serial
capture subsequently showed a new boot and successful connection. These logs do
not establish the original cause or prove a 12-hour stability fix.

Code inspection found that each reconnect replaced its NimBLE callback allocation
without freeing the previous object. The touchscreen now keeps one client/callback
pair and refreshes GATT services when reconnecting. Controller name matching is
exact, avoiding other BlueSquid touchscreens. Scan failures are throttled and scan
address handoff is protected while queuing connection work.

An active reconnect job gets a disconnect request after 30 seconds. If it remains
blocked at 60 seconds, the touchscreen restarts; tasks are not forcibly deleted
while NimBLE can still reference their stack. An offline Controller by itself does
not cause restarts. The Controller firmware and output state are not changed.

Every 30 seconds the touchscreen reports link/scanning state, reconnect stage,
snapshot age/count and free/minimum heap. Startup includes firmware and reset reason.
Native watchdog tests cover expiry, completed jobs, 12-hour offline time and millis
wraparound. A full overnight test is still required.

Serial ports at 115200 baud:
- Touchscreen: `/dev/cu.usbmodem5B3E0831171`
- Controller: `/dev/cu.usbmodem5B8F0430551`

Backup: `archives/BlueSquid_before_ble_recovery_20260918.tar.gz`.

## Open the correct serial monitor

In VS Code use **Terminal → Run Task → BlueSquid: Monitor Touchscreen** or
**BlueSquid: Monitor Controller**. These tasks explicitly select the environment
and read its port from `platformio.local.ini`; they do not depend on the currently
selected build environment. Stop the previous monitor with Ctrl+C before changing
devices. An already running monitor does not change ports when the build selection changes.

Equivalent commands:

```sh
pio device monitor -e touchscreen_controller --filter time --filter log2file
pio device monitor -e main_controller --filter time --filter log2file
```

Each monitor writes a timestamped capture under `logs/` (excluded from Git).
Start the two monitors at different times; PlatformIO names captures to the second.
Opening serial ports can reset some USB-to-UART boards, so leave a capture running
for overnight diagnosis rather than opening it only after a failure.

## Progress-aware watchdog — touchscreen 1.0.25

The saved overnight log showed subscription activity and fresh snapshots when the
1.0.24 watchdog disconnected the link at 30 seconds of total connection time.
The watchdog now measures **30 seconds without observed progress**. Successful
setup steps and validated snapshots advance an atomic progress counter. Slow
connections can exceed 30 or 60 seconds overall while still making progress.

After an actual inactivity timeout, a disconnect is requested once. If the worker
has not finished 30 seconds after that request, the touchscreen restarts. Late
notifications cannot indefinitely delay this cancellation recovery. An offline
Controller with no active reconnect job still never triggers this watchdog.

Regression tests reproduce the observed delayed subscription/fresh-data sequence,
then verify stalled progress, cancellation, completed jobs, deadline-edge progress,
and timer/counter wraparound. This change corrects the premature watchdog timeout;
it does not address the separate repeated-scanning discovery failure.

Backup: `archives/BlueSquid_before_progress_watchdog_20260920.tar.gz`.
