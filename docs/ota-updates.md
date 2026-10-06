# Browser firmware updates

Available starting with Controller 1.0.22 and Touchscreen 1.0.33.
Use the touchscreen as the update entry point for both devices.

## One-time installation

Install both targets by USB before using OTA:

```sh
pio run -e main_controller -t upload
pio run -e touchscreen_controller -t upload
```

Use each device's USB port from `platformio.local.ini`. Both targets need this
initial installation; earlier firmware cannot establish the update relay.
The touchscreen migrates from `huge_app.csv` to `partitions/touchscreen_ota.csv`,
with two 6 MiB application slots on its 16 MB flash. NVS stays at the same
offset and size, preserving settings and BLE bonds. Its former SPIFFS area is
repurposed; the current application does not use it. SD-card files are
unaffected. Do not erase the entire flash. The Controller retains its existing
8 MB default OTA layout.

## Update both devices from the touchscreen

1. Connect your phone or computer to the BlueSquid **System hotspot**.
2. Open `http://192.168.4.1:8080/`, or the address under **System Configuration
   → Firmware updates**. The phone remote also links to this page.
3. The page opens without a separate username/password prompt on Controller
   1.0.24 and Touchscreen 1.0.35 or later. Earlier versions still use the System
   hotspot SSID and Wi-Fi password as the page login.
4. Choose **Controller**, select its `.bsfw` package and press **Install update**.
   Stay on the BlueSquid hotspot. The page prepares the connection and transfers
   the file; the Controller checks it and restarts. The touchscreen stays online.
5. Choose **Touchscreen** on the same page and install its package. The touchscreen
   restarts, so reconnect to the hotspot if needed and reload to confirm versions.

The page shows installed versions when the Controller is connected over BLE.
These are two separate updates; installing one file does not update both devices.
Update the Controller first so the touchscreen remains available for the relay.
Keep the van parked and power connected until each update finishes.

Build the matching packages with:

```sh
pio run -e main_controller
pio run -e touchscreen_controller
```

| Device | Generated package |
| --- | --- |
| Controller | `.pio/build/main_controller/BlueSquid-main_controller.bsfw` |
| Touchscreen | `.pio/build/touchscreen_controller/BlueSquid-touchscreen_controller.bsfw` |

## Connection and credentials

The Controller normally connects to the Cerbo hotspot. For an update, an
encrypted BLE request temporarily moves it to the touchscreen hotspot. The
browser sends the package to the touchscreen, which streams it to the
Controller over Wi-Fi. No SD card or full-image RAM buffer is required.
BLE handles setup/status, not firmware data. The phone/computer never needs
to join the Cerbo network or know the Controller's IP address.

Cerbo readings pause while the Controller is on the update network. Its saved
Cerbo SSID/password are not replaced. It returns to Cerbo after cancellation,
failed setup, or reboot; an abandoned session expires after ten minutes.
Initial Wi-Fi connection failure restores Cerbo after thirty seconds. The
normal device control loops continue during uploads, but each successful
update restarts the affected device and briefly interrupts its connections.
Output restoration follows existing firmware behavior.

The touchscreen synchronizes its saved System hotspot credentials to the
Controller over an encrypted BLE characteristic. The Controller acknowledges
only after saving them. Reconnects and changes trigger synchronization, and
the update connection waits for it. Before first synchronization, the Controller
uses the shared hotspot build defaults. Changes made while an upload is active
apply afterward. Passwords are never printed in startup logs or returned by BLE
reads; credential acknowledgements contain only a request ID.

The Controller's direct updater remains available at `http://<Controller IP>:8080/`
from its current Wi-Fi network, without a separate page login. This is
an alternative for diagnostics; normal updates use the touchscreen page.

## Validation and recovery

Packages include target identity, exact image length and SHA-256. The browser
checks the package target and length. Each receiving device rejects malformed,
oversized, truncated, extra-data and corrupted packages before activating them.
Raw `.bin`, bootloader, partition-table and merged-flash files are not accepted.

For a relayed update, the touchscreen also checks the target and checksum and
withholds the final upload boundary until the entire browser request is valid.
The Controller independently verifies the package and ESP application image.
The touchscreen reports success only after the Controller returns HTTP 200.
A broken connection before finalization leaves the previous firmware selected.
If the final acknowledgement is lost, check the installed version before retrying:
the update may already have completed.

Access relies on Wi-Fi/network access; anyone able to reach port 8080 can use
the updater, including on the Controller’s Cerbo network. Uploads and network
changes still require the page’s anti-CSRF token. Do not expose port 8080 to
the Internet. The checksum detects corruption; it is not a firmware
signature. Install packages from trusted builds.

The bundled frameworks enable bootloader rollback, but Arduino confirms the
image before application setup by default. This implementation does not defer
that confirmation or add application health checks. If new firmware cannot
start its updater or BLE connection, recover using USB. Bootloader/partition
layout changes also require USB.

The implementation follows Espressif's [inactive-slot OTA workflow](https://docs.espressif.com/projects/esp-idf/en/v4.4.3/esp32s3/api-reference/system/ota.html).

## Verification

`bash tests/ota/run.sh` covers packages, fragmented transfers, target/size checks,
truncation/extra bytes, write/checksum/image failures, abort/retry handling,
credentials, session wire format and lease timing, including timer rollover.
`bash tests/ota_network/run.sh` tests the actual Cerbo Wi-Fi manager with a fake
radio: update joins, cancellation, saved-setting preservation, deferred changes,
failed joins and abandoned-session recovery. Both PlatformIO targets and browser
JavaScript syntax are also checked.

Live browser transfers, boot confirmation and reconnect behavior on the installed
hardware remain to be verified after the one-time USB installation.
