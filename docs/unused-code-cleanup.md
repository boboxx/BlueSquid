# Unused code cleanup

Controller 1.0.16 and touchscreen 1.0.23 remove superseded hardware paths:

- PCA9685 manager, dependency, address/frequency configuration, startup probe,
  unused primary I2C initialization and PWM fan fallback.
- Private touchscreen CAN and RS-485 transports, serial framing, build switches,
  diagnostics and the old Cerbo RS-485 bridge script.
- Unused CAN frame IDs and serialization helpers. Shared BLE command numbers are
  unchanged, with declarations now in `BlueSquidControlProtocol.h`.

BLE links, SP630E assignments, RV-C fan control and Cerbo Wi-Fi/Modbus remain active.
The stored device record retains its byte layout with one reserved byte, so existing
lighting and accessory configuration does not reset. Fan state is never restored or
sent at boot. Calls without an RV-C handler fail instead of falling back to hardware.
Sensor wiring and optional direct GPIO output support are retained.

Backup: `archives/BlueSquid_before_unused_code_cleanup_20260917.tar.gz`.
