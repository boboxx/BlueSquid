# Touchscreen BLE recovery - 1.0.24

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

## Progress-aware watchdog - touchscreen 1.0.25

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

## Discovery recovery - touchscreen 1.0.26

All three recovery measures are now present: the progress-aware reconnect watchdog,
scan-state recovery with a direct retry, and a bounded restart fallback.

After a previously connected Controller goes offline, every 15 seconds without an
active connection job the touchscreen stops scanning, clears scan results and the
duplicate cache, removes that disconnected peer from NimBLE's ignored-address
list, and directly retries its last successfully connected address. Only a successful
connection supplies this address; arbitrary scan candidates do not replace it.
If stopping the scan fails, cache clearing and the direct connection are deferred.

After two minutes offline, and only when no link or connection job is active, the
touchscreen may restart once. The restart budget survives software resets and is
rearmed only after a minute of continuous online operation. A fresh boot without a
successful connection keeps scanning without discovery-triggered reboot loops.
The remembered address is in RAM; a reboot performs normal discovery again.

Tests cover first-boot offline operation, retry intervals, active-job exclusion,
restart suppression, recovery, and timer wraparound. Physical overnight stability
still requires verification. The Controller and existing output settings are unchanged.
Backup: `archives/BlueSquid_before_discovery_recovery_20260920.tar.gz`.

## Sleep-related link loss - touchscreen 1.0.37

The October 6 capture contains five Controller-link disconnects. Three occur
4–5 seconds after `Display asleep`. Reconnection takes about 40–110 seconds;
status notifications resume before all GATT setup completes, but the UI correctly
keeps controls offline until setup is complete. Snapshot counters continue
increasing throughout this capture. There is no boot banner, panic, or explicit
restart message in it, so it cannot establish the reported reboot cause.

Previously, screen sleep requested a 100–200 ms BLE interval and a four-second
supervision timeout. The on-connect callback also requested that slower interval
before service discovery when the display was sleeping. Version 1.0.37 removes
sleep/wake BLE renegotiation and keeps the initial 15–30 ms requested interval
through setup and normal operation. Display sleep still switches off the backlight;
it no longer changes radio timing. This may use more radio power while sleeping.

A GAP event listener now records the actual disconnect reason and the negotiated
interval, latency and supervision timeout. It does not use `getLastError()` as a
disconnect reason, because that API reports the last client operation error.
Existing watchdog and discovery recovery remain in place.

Recovery and display tests plus the touchscreen build are checked. Repeated
sleep/wake cycles and an overnight capture on the installed hardware are still
needed to validate the mitigation. Controller logs at matching times and the
actual touchscreen startup/crash output are needed if a reboot recurs.

## Confirmed recovery restart - touchscreen 1.0.38

The extended October 6 log confirms a deliberate recovery restart at 08:58:54:
`BLE discovery recovery exhausted after 120 seconds; restarting touchscreen once`,
followed by `RTC_SW_CPU_RST` and the 1.0.36 boot banner. No panic or brownout is
shown. The Controller connection had completed at 08:58:30, before another
sleep-related disconnect at 08:58:54. Startup then reconnects successfully.

Discovery recovery previously cleared its outage timer only when main-task
polling observed `online=true`. Background GATT maintenance reserves the same
connection state and can defer that polling across an entire brief reconnection.
Consequently, a subsequent disconnect can inherit an already-expired outage.

Successful connection completion now atomically latches a recovery event from
the worker. The main-task recovery policy consumes it before evaluating timeout,
even if the link is already offline again. A fresh outage gets its own timer;
the bounded restart fallback and restart budget remain intact. The regression
test covers recovery missed by main polling followed by a new disconnect.

Version 1.0.38 also includes the unflashed 1.0.37 sleep-timing mitigation. Only
`touchscreen_controller` needs uploading. Repeated sleep/wake and overnight
hardware verification remain required.

## Controller radio activity capacity (1.0.26)

The ESP32-S3 Arduino SDK initializes six radio activities independently of
NimBLE's six host connection slots. `BleRadioCapacity.cpp` wraps controller
initialization to request the SDK-supported maximum (ten activities), leaving
room for connections and discovery/advertising. The main_controller linker
flag enables this wrapper; no framework or dependency files are patched.
Startup logs both the previous/new activity budget and the host slot count.

Commissioning exposed HCI error 519 (0x207, memory capacity exceeded) when
adding a third SP630E alongside the touchscreen. Unplugging a working SP630E
allowed the new unit to connect, but that unit still returned no notifications
after acknowledged status queries. Capacity and missing status replies must
be verified separately; successful GATT writes do not make an output available.

With all three modules powered again, 1.0.26 allowed the third module to
reconnect repeatedly while the other two and touchscreen remained connected;
error 519 did not recur during the observed test.

Controller 1.0.27 adds one read-only status query without GATT response per
silent connection, after normal queries and subscription recovery. It does not
change output commands or availability deadlines. Live testing of module
25:79 still showed zero notifications despite the probe being queued. This is
a diagnostic fallback, not a confirmed fix for that module's missing replies.

Controller 1.0.28 enumerates service UUIDs and characteristic capabilities once
per module per boot, before retaining command pointers. The live comparison
found identical GATT layouts on the new 25:79 and working 26:fa modules:
FFE0 / FFE1 at value handle 20 (write, write-without-response, notify), and
CCCD handle 21 enabled as 01 00. Both also expose the same 5833ff01 service
with ff02 write and ff03 notify characteristics. No alternate lighting status
endpoint was established by this comparison. The new module remains silent;
further protocol changes need evidence from manufacturer-app traffic.

## Extended status notification MTU (Controller 1.0.29)

An isolated macOS CoreBluetooth query to replacement module 45:5a returned
three valid 186-byte status replies (payload length 0xB4 plus six-byte header).
The previous requested ATT MTU of 185 allowed only 182 notification bytes.
On the ESP32 these modules sent no notifications rather than a short packet.
The Controller now requests MTU 247 using Sp630eProtocol::kPreferredMtu.
The regression test checks extended framing, rejection of a truncated reply,
and sufficient notification capacity. Live testing confirmed MTU 247 and valid
status from all three modules, including 45:5a. The original 25:79 still needs
retesting with this firmware. The replacement reports configuration 0x85 (RGB)
and firmware 4.0.26 in its captured reply; RGBCCT requires configuration in the
manufacturer app appropriate to the attached strip.
