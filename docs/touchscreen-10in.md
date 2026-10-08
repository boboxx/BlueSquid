# 10.1-inch touchscreen

`touchscreen_10in` builds the touchscreen firmware for the Waveshare
ESP32-P4-WIFI6-Touch-LCD-10.1 from the same sources as the 7-inch
`touchscreen_controller`. Both report the touchscreen firmware version.

```sh
pio run -e touchscreen_10in -t upload
```

## Hardware

| Part | 10.1-inch board |
| --- | --- |
| Processor | ESP32-P4, silicon v3.x (`board = esp32-p4_r3`), 32 MB flash and PSRAM |
| Display | JD9365, 800x1280 portrait, two-lane MIPI-DSI, reset GPIO27 |
| Backlight | PWM on GPIO26 |
| Touch | GT9271 (GT911 driver), I2C SDA GPIO7 / SCL GPIO8, address 0x5D or 0x14 |
| MicroSD | SDMMC slot 0: CLK 43, CMD 44, D0-D3 39-42 |
| Radio | ESP32-C6 over SDIO through ESP-Hosted (Wi-Fi and BLE) |

Pin assignments and the panel initialization table come from Waveshare's
[ESP32-P4-WIFI6-Touch-LCD-X](https://github.com/waveshareteam/ESP32-P4-WIFI6-Touch-LCD-X)
board support, which targets Arduino-ESP32 3.3.11.

## Differences from the 7-inch build

- **Display.** The UI keeps its 800x480 layout. The P4's PPA scales each
  redrawn area by 25/16 and rotates it into the landscape panel, giving a
  1250x750 picture with a thin black border. Redraws are aligned to 16 pixels
  so scaled areas meet without seams. Touches are mapped back the same way.
- **Orientation.** `BLUESQUID_P4_DISPLAY_ROTATION` in `platformio.ini` selects
  90 or 270 degrees. Use the other value if the picture is upside down.
- **BLE.** NimBLE-Arduino needs an on-chip controller, which the P4 lacks. This
  target uses the Arduino core BLE library, which reaches the C6 through
  ESP-Hosted. `src/touchscreen/TouchBleStack.h` selects the library.
- **SD card.** Native SDMMC (one-bit mode, as in Waveshare's example) replaces
  the 7-inch board's SPI and CH422G chip select.
- **OTA.** Packages carry device ID 3, so 7-inch and 10.1-inch images cannot
  be installed on the wrong screen.

## First power-up checklist

The firmware compiles but has not yet run on this board. On the first USB
upload, watch the serial log at 115200 baud:

1. Check the silicon revision with `esptool --chip esp32p4 chip-id`. v1.x
   silicon needs `board = esp32-p4` instead of `esp32-p4_r3`.
2. `Touch controller found at 0x5D` (or `0x14`). If neither answers, touch
   input will not work.
3. `LVGL init: 800x480 UI scaled 25/16 onto 800x1280 panel`. Then check the
   picture orientation and that touches land under the finger.
4. BLE: the screen must reach the Controller. Pair it as a second touchscreen
   from System Configuration on an already paired screen. If `BLE init failed:
   ESP32-C6 radio unavailable` appears, the C6's factory hosted firmware may
   not match Arduino 3.3.11's ESP-Hosted version.
5. Settings export and import to the SD card.

## Build environment notes

All three targets use pioarduino 55.03.312-1 with Arduino-ESP32 3.3.12
(ESP-IDF 5.5.5). The board support remains based on Waveshare's Arduino
3.3.11 examples. Both touchscreen targets use LVGL 9.6.0, ESP32 IO Expander
1.1.1, ArduinoJson 7.4.3 and esp-lib-utils 0.2.3. Display Panel 1.0.4 requires
esp-lib-utils below 0.3.0, so that incompatible release is not selected.

The Controller and 7-inch touchscreen use NimBLE-Arduino 2.5.1. On the first
boot after upgrading, legacy pairing records are converted once before BLE
starts, followed by an automatic restart. Unknown record formats stop BLE
initialization rather than discard existing pairings. See
[dependency updates](dependency-updates.md) for backups and rollback details.

Run a clean build after changing framework versions. Explicit Arduino package
overrides are omitted: the pinned platform supplies its matching core/libraries
and applies its build-script fix.
