# BlueSquid ESP32-S3 wiring

This map describes the `main_controller` and `touchscreen_controller` builds.
GPIO signals are 3.3 V logic. Loads require suitable protected drivers.

## Controller

| Connection | GPIO / interface | Notes |
| --- | --- | --- |
| HTU21D SDA | 10 | Dedicated climate I2C bus, 100 kHz |
| HTU21D SCL | 11 | Sensor address 0x40 |
| HTU21D supply | 3V3 / GND | Common ground |
| GY-61 / ADXL335 X | 4 | Analog input |
| GY-61 / ADXL335 Y | 5 | Analog input |
| GY-61 / ADXL335 Z | 6 | Analog input |
| GY-61 supply | 3V3 / GND | Outputs must remain within ESP32 input limits |
| All-lights momentary switch | 35 to GND | Active low, internal pull-up; uses the configured light group |
| Accessory 3 driver | 36 | Active high; use a protected load driver |
| Accessory 4 driver | 39 | Use a protected load driver |
| RV-C transceiver TXD | 37 | Controller CAN TX |
| RV-C transceiver RXD | 38 | Controller CAN RX |
| RGBW lights and assigned accessories | SP630E over BLE | Configure device/channel assignments on the touchscreen |
| Touchscreen | BLE | No wired touchscreen CAN or RS-485 link |
| Cerbo GX | Wi-Fi / Modbus TCP | Enable Modbus TCP on Cerbo; enter credentials in settings |

The PCA9685 driver and its GPIO 8/9 I2C initialization have been removed.
The climate sensor remains on GPIO 10/11; no rewiring is required.
Local RGBW GPIO outputs are disabled by default (`-1`); production assignments
use SP630E controllers. USB and pump direct GPIO outputs are also unassigned.

For RV-C transceiver supply/logic compatibility, termination and commissioning,
see [FA75 RV-C setup](rvc-fa75.md). The fan retains its own fused power supply.

## Touchscreen

Use the Waveshare ESP32-S3-Touch-LCD-7 board selected in `platformio.ini`.
The display, touch panel, backlight and SD-card wiring use the supported board
configuration. Flash and monitor through its USB-to-UART connector at 115200 baud.
The touchscreen connects to the Controller over BLE and provides a Wi-Fi hotspot
for the phone web remote.

## Configuration

Private Wi-Fi defaults and USB port selections belong in `platformio.local.ini`.
Saved device settings override build defaults. See [network configuration](network-v1.md).
Hardware pin constants are in `include/AppConfig.h`; active overrides are in
`platformio.ini`.

The simulated hardware, sensor and battery options remain available for testing.
The current build selects real sensor and battery paths. Calibrate vehicle level
through the touchscreen after installing the GY-61.
