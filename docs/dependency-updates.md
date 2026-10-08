# Dependency update — October 8, 2026

Controller firmware 1.0.40 and touchscreen firmware 1.0.60 share Arduino-ESP32
3.3.12 / ESP-IDF 5.5.5 through pioarduino 55.03.312-1. NimBLE-Arduino is pinned
to 2.5.1 on the Controller and 7-inch screen; the 10.1-inch screen retains the
Arduino BLE library for its ESP-Hosted radio. Both screens use LVGL 9.6.0,
ArduinoJson 7.4.3, IO Expander 1.1.1, Display Panel 1.0.4 and esp-lib-utils 0.2.3.
The display driver's declared requirement excludes esp-lib-utils 0.3.0.

## BLE migration

The first boot inspects the legacy bond records before starting NimBLE. Known
1.x records are converted with NimBLE's migration helper, a completion marker
is saved, and the device restarts. Later boots skip conversion. Fresh installs
and already-current records do not reset the local identity key. Unknown bond
formats or storage failures leave BLE stopped and report an error.

The Controller retains six host connection and bond slots. Arduino's generated
configuration is included before applying the Controller-specific overrides in
`include/BleBuildConfig.h`; static assertions check the host slot counts and
20 ms receive-credit interval. The radio activity wrapper is retained.

Client timeouts and scan durations now use milliseconds. Scan callbacks,
connection callbacks, advertising names and service enumeration use NimBLE's
2.x APIs. The Controller's optional PWM outputs use Arduino 3.x LEDC APIs.

## Backup and rollback

Local backups are excluded from Git under
`archives/before-remaining-updates-2026-10-08/`:

- `source.tar.gz`: pre-update working files, including local configuration and
  uncommitted work; excludes Git metadata, build cache and older archives.
- Each target's folder: available pre-update firmware/OTA artifacts.
- Connected Controller and 7-inch folders: `nvs.bin`, read directly from each
  device at offset `0x9000`, length `0x5000`, before migration.

These backups contain private configuration and pairing keys. Keep them local.

To roll back a migrated device, restore its own saved firmware and its own NVS
backup together. Restoring only a NimBLE 1.x firmware image is insufficient
because the bond record layout changed. Never apply one device's NVS backup to
the other device. Do not erase the entire flash as part of a routine update.

## Hardware checks

After uploading, verify that the Controller and screen reconnect using their
existing bond, all configured SP630E modules reconnect and respond, and the
screen's icons, touch input, SD import/export and OTA work. A successful build
and native tests do not establish display or radio behavior on an unconnected
10.1-inch board.

## Validation for this batch

All three firmware targets built successfully; all 13 native test suites passed.
The connected Controller and 7-inch touchscreen were flashed with verified
hashes. A 25-second serial capture showed the screen migrating its bonds,
restarting once, reconnecting and receiving a configuration acknowledgement.
The Controller reported an encrypted, bonded, trusted connection and successful
GATT responses from all three SP630E modules. This is a startup check, not a
long-duration radio or complete visual/touch/SD/OTA validation. The 10.1-inch
board was not connected and remains unflashed.
