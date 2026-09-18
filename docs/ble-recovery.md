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
