# BlueSquid ESP32-S3 Wiring

This wiring map matches the `rear_controller` and `touchscreen` build targets.
All ESP32-S3 GPIO signals are 3.3 V logic. Use suitable MOSFETs, transistor
drivers, relays, optocouplers, and common grounds where required.

## ESP32-S3 to modules

| Module | Module pin / signal | ESP32-S3 GPIO | Notes |
| --- | --- | ---: | --- |
| PCA9685 PWM driver | SDA | 8 | Main I2C bus, 400 kHz |
| PCA9685 PWM driver | SCL | 9 | Main I2C bus, 400 kHz |
| PCA9685 PWM driver | VCC | 3V3 | Logic supply |
| PCA9685 PWM driver | GND | GND | Common ground |
| HTU21D climate sensor | SDA | 10 | Separate I2C bus because HTU21D and PCA9685 both use address `0x40` |
| HTU21D climate sensor | SCL | 11 | Separate I2C bus, 100 kHz |
| HTU21D climate sensor | VIN / VCC | 3V3 | Use 3.3 V logic |
| HTU21D climate sensor | GND | GND | Common ground |
| GY-61 / ADXL335 | X | 4 | Analog input |
| GY-61 / ADXL335 | Y | 5 | Analog input |
| GY-61 / ADXL335 | Z | 6 | Analog input |
| GY-61 / ADXL335 | VCC | 3V3 | Do not power from 5 V unless the breakout explicitly regulates to 3.3 V output levels |
| GY-61 / ADXL335 | GND | GND | Common ground |
| Front RGBW strip | External Triones BLE controller | No ESP32 GPIO | Zone 1 address in `platformio.ini` |
| Bed RGBW strip | External Triones BLE controller | No ESP32 GPIO | Zone 2 address in `platformio.ini` |
| All-lights momentary switch | Switch input | 35 | Active low; wire switch between GPIO35 and GND |
| All-lights momentary switch | Other side | GND | Internal pull-up is enabled in firmware |
| Generic accessory output 3 driver | Control input | 36 | Active high prototype output; use a protected driver appropriate to the assigned load |
| Generic accessory output 3 driver | Control ground | GND | Prototype assignment only; production output uses the I/O expansion/driver stage |

## Cerbo GX VE.Can energy link

Victron products connect to the Cerbo GX. The rear controller receives the
Cerbo's published energy data over channel 2 of the isolated dual-MCP2515 CAN
board. Channel 2 shares GPIO10 SCK, GPIO11 MISO and GPIO12 MOSI, and uses GPIO14
CS plus GPIO16 interrupt.

Use a passive verified Victron RJ45 pigtail or connector labelled
`VE.CAN - NOT ETHERNET`:

| Victron RJ45 pin | Signal | Connection |
| ---: | --- | --- |
| 7 | CAN-H | Isolated channel 2 CAN-H |
| 8 | CAN-L | Isolated channel 2 CAN-L |
| 3 | NET-C/reference | Bus-side reference if required by the selected interface |
| 6 | NET-S supply | Do not connect to ESP32 5 V or 3.3 V rails |

Configure the applicable Cerbo VE.Can port/profile and confirm its bit rate.
The expected rate is 250 kbit/s. Use exactly two 120-ohm terminators across the
complete VE.Can segment, one at each physical end. Never join VE.Can to the
RV-C network.

### Temporary RS-485 development bridge

The existing Cerbo bridge files and isolated 115200-baud RS-485 link may remain
available for development until VE.Can firmware is complete. Rear UART RX is
GPIO1 and TX is GPIO2. This is not the production Victron interface.

The Cerbo bridge files are in `cerbo/`. Copy `bluesquid-rs485.py` to
`/data/bluesquid-rs485/`, copy the service directory to
`/data/service/bluesquid-rs485/`, and make all three files executable. The
service defaults to `/dev/ttyUSB0`; change `cerbo/service/run` if the adapter has
a different device name. The `/data` placement is intentional so the files are
kept separate from the read-only operating-system image.

The wire frame is ASCII and checksum protected:

```
$BSQ,1,sequence,validMask,V,I,P,SOC,CE,TTG,solarW,solarState,dcdcW,dcdcState*HH
```

`HH` is the two-digit XOR of every character between `$` and `*`. The valid-mask
bits are SmartShunt=1, solar=2, and DC/DC=4. Values use volts, amps, watts,
percent, amp-hours, and minutes.

## Rear RV-C CAN bus

The rear controller, future BlueSquid nodes and compatible RV-C equipment share
one physical CAN network. The touchscreen no longer connects to this bus; its
control link is BLE. Use a 120-ohm twisted pair plus the
required reference. Run one linear trunk with short stubs, not a star, and fit
exactly one 120-ohm terminator at each physical end of the complete bus.

The rear controller uses channel 1 of the isolated dual-MCP2515 CAN board.
Both MCP2515 channels share GPIO10 SCK, GPIO11 MISO and GPIO12 MOSI. Channel 1
uses GPIO13 CS and GPIO15 interrupt. Channel 2 uses GPIO14 CS and GPIO16
interrupt and is the electrically separate Victron VE.Can interface.

The Waveshare display's onboard CAN transceiver is unused by the current BLE
firmware and remains available as a future service fallback. Use the separate
CH343 USB-to-UART connector for flashing and serial logs. Power the display from
a fused, protected 12-to-5 V buck converter rather than directly from the
vehicle's 12 V system.

RS-485 and CAN are separate cables and protocols:

```text
Victron devices -> Cerbo GX -> isolated VE.Can channel 2 -> rear ESP32-S3
touchscreen ESP32-S3 <-> persistent bonded BLE <-> rear ESP32-S3
rear ESP32-S3 -> isolated CAN channel 1 -> RV-C nodes
```

<!-- Legacy JK notes retained below only for older hardware revisions.

The JK-B2A8S30P battery monitor is polled over Bluetooth LE, so it does not use
an ESP32-S3 GPIO pin. Keep the BMS Bluetooth enabled and near the ESP32-S3.

The firmware uses the same JK BLE frame format as the ESPHome JK integration:
passive BLE scan, connect to a configured BMS address, subscribe to `FFE1`
notifications, and send the `0x96` live-data request on `FFE1`. For multiple
batteries, BlueSquid samples one fresh live frame from one BMS, disconnects, and
moves to the next configured address. The reported cell count comes from the BMS
enabled-cell bitmask, so a 4S or 8S pack publishes only its active cells instead
of always assuming 8.

For testing, fully disconnect or close the JK phone app before starting the MCU.
Many JK Bluetooth modules only allow one active central connection, so the phone
app can prevent BlueSquid from connecting even though the battery is healthy.
Use the serial monitor at 115200 baud to confirm whether the MCU found the BMS,
connected, or timed out waiting for cell data.

If the monitor shows `NimBLEClient: Connection failed; status=574`, the ESP32
did not complete the Bluetooth connection. Fully close the JK app, wait about
30 seconds, then reset the ESP32. The firmware intentionally waits between
retries so the JK Bluetooth module can leave its previous connection state.

If the monitor shows `Configured JK BMS ... was not seen during scan`, the ESP32
did not see that Bluetooth address advertising during its scan window. Fully
close the JK app, wake the BMS if needed, keep the ESP32 near the battery, and
reset the ESP32. If it still does not appear, open the JK app only long enough
to confirm the BMS address, then close the app again before testing BlueSquid.

If the monitor shows `No JK BMS found during scan` and no address is configured,
open the JK app and note the BMS Bluetooth address, then set the optional build
flag in `platformio.ini`:

```ini
; -DBLUESQUID_JK_BMS_ADDRESS=\"aa:bb:cc:dd:ee:ff\"
```

Replace the example address with the BMS address, remove the leading semicolon,
flash again, then close the JK app before testing BlueSquid. For multiple
batteries, use a semicolon-separated list:

```ini
-DBLUESQUID_JK_BMS_ADDRESSES=\"aa:bb:cc:dd:ee:ff;11:22:33:44:55:66\"
```

The iOS Settings > Battery monitors list writes this same address list into MCU
flash. Once saved, the MCU keeps polling the BMS modules without the iOS app
running.

The iOS app can also ask the MCU to discover nearby BMS modules from Settings >
Battery monitors > Discover BMS. The scan runs on the MCU because iOS does not
expose the real BLE MAC address that the MCU needs for polling. Select a
discovered address, then tap Save to store it on the controller.

The JK BLE reader uses the `JK02_32S` AA55 frame layout used by many current
JK BMS units.
-->

## GY-61 mounting direction

Mount the GY-61 flat and fixed to the camper body, not to a loose panel. Use
the module axes like this:

| Van direction | GY-61 axis | Firmware reading |
| --- | --- | --- |
| Front to back | X | Pitch |
| Side to side | Y | Roll |
| Up and down | Z | Gravity reference |

If the displayed pitch or roll moves the opposite direction from what feels
natural after mounting, flip `kInvertPitch` or `kInvertRoll` in
`include/AppConfig.h`.

Use level calibration after the camper is parked in the position you want to
treat as zero. The camper does not need to be perfectly level before
calibration; calibration stores the current pitch and roll as the reference.

As a practical target, within 1 degree feels very level, within 2 degrees is
usually acceptable for sleeping and general camping, and 3 degrees or more is
noticeable. If using a propane absorption fridge, follow that fridge's manual;
many are less tolerant than people are.

## PCA9685 output channels

The PCA9685 is used for the general camper outputs managed through
`PwmManager`.

| PCA9685 channel | Function |
| ---: | --- |
| 0 | PWM Output 1 |
| 1 | PWM Output 2 |
| 2 | PWM Output 3 |
| 3 | PWM Output 4 |

Keep these names on the PCB and terminal labels. Assign camper-specific names
such as `Front pot lights` or `Ventilation fan` from the touchscreen's
**Menu → Device labels** page. This allows loads to move between channels
without changing the PCB design or display firmware.

## Firmware switches

`BLUESQUID_SIMULATED_HARDWARE=1` currently keeps the PCA9685 path simulated, so
PCA9685 channel outputs will not drive physical hardware yet. Set it to `0` in
`platformio.ini` when the PCA9685 board is wired and ready.

`BLUESQUID_SIMULATED_BATTERY=0` enables real JK BMS Bluetooth polling even while
other hardware remains simulated.

`BLUESQUID_SIMULATED_SENSORS=0` means the HTU21D and GY-61 sensor paths are
already configured for real hardware.

`BLUESQUID_CEILING_LED_PIN=-1` disables the old development-board LED mirror.
