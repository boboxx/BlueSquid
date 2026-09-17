# BlueSquid Camper Control

BlueSquid connects an ESP32-S3 Controller and a Waveshare ESP32-S3-Touch-LCD-7
using BLE. It provides lighting and accessory controls, climate and level sensing,
Victron Cerbo GX monitoring over Wi-Fi/Modbus TCP, and an RV-C roof-fan client.

## Build

Install PlatformIO Core. Private build defaults and machine-specific USB ports
belong in `platformio.local.ini`, which Git ignores. An optional template is provided:

```sh
cp platformio.local.example.ini platformio.local.ini
pio run -e main_controller
pio run -e touchscreen_controller
```

Configure Wi-Fi in the touchscreen settings or in the local file. Saved device
settings take precedence over build defaults. The default phone hotspot password
is `1234567890`; set your own in System Configuration → System hotspot.

The configured Controller USB port is a USB-to-UART bridge (VID:PID `1A86:55D3`).
Its firmware uses `ARDUINO_USB_CDC_ON_BOOT=0` so BlueSquid `Serial` messages
appear on that port at 115200 baud. Enabling native USB CDC routes those
messages to a different interface even though library warnings may still
appear on the UART bridge.

The project default is `main_controller`. When flashing the display, explicitly
select the `touchscreen_controller` environment; compiling alone does not write
the firmware to the screen:

```sh
pio run -e touchscreen_controller -t upload
```

The Waveshare display firmware uses the board's CH343 USB-to-UART connector for
flashing and 115200-baud startup logs. A successful boot prints `Display hardware
initialized`, `LVGL initialized`, and `Touchscreen UI ready`. This target is only
for a board labelled `ESP32-S3-Touch-LCD-7`.

## Features

- Four RGBW light controls with SP630E full-device or individual-channel assignments.
- Accessory controls and a configurable Home / GPIO 35 light group.
- Battery, power and charging information from Cerbo GX over Wi-Fi/Modbus TCP.
- Temperature, humidity and vehicle level sensing.
- RV-C Dometic FA75 fan power, speed and reverse airflow controls. Physical fan
  validation is pending; see [RV-C setup](docs/rvc-fa75.md).
- Touchscreen settings, JSON configuration backup and a phone web remote.

Hardware assignments and application constants are centralized in
`include/AppConfig.h`. The current ESP32-S3 module wiring map is documented in
`docs/wiring.md`.

## Firmware targets

- `main_controller`: Controller ESP32-S3, sensors, accessory outputs, primary BLE
  control server, SP630E control, RV-C fan client and Cerbo Wi-Fi/Modbus monitoring.
- `touchscreen_controller`: Waveshare 7-inch ESP32-S3 display, LVGL 9.5
  interface and persistent BLE client. BlueSquid supplies its own LVGL 9 display/touch port
  because ESP32_Display_Panel 1.0.4's bundled GUI adapter targets LVGL 8.

The optimized touchscreen BLE protocol is in `include/BlueSquidBleProtocol.h`.
CAN connections and termination are documented in `docs/wiring.md`.
The complete proposed hardware, cost and implementation review is in
`docs/system-overview.md`.

Local backups, build outputs and private configuration are excluded from this repository.
