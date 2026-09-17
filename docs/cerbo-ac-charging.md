# AC charging telemetry

The Power dashboard displays VE.Bus charger DC output alongside solar and
DC/DC. System unit 100 register 866 is signed watts at `/Dc/Vebus/Power`:
positive values represent charging and negative values represent inverter
draw. The AC charger field displays the positive portion, not total AC input
or AC load consumption. VE.Bus register 31 supplies the operating state,
using the same configured VE.Bus unit ID as the Shore charger control.

Both reads are optional: failed reads do not invalidate the battery snapshot.
A failed power read or stale Cerbo connection marks AC telemetry invalid;
the dashboard displays `0 W` and `--`, matching the other charger fields.
Unknown operating state displays `--`. The inferred DC-load fallback includes
positive charger output; measured system DC load remains preferred.

Source: https://www.victronenergy.com/upload/documents/CCGX-Modbus-TCP-register-list-3.73.xlsx

BLE v1 snapshot extension: bytes 84–85 unsigned charging watts, byte 86 VE.Bus
state (255 unknown), byte 87 power validity. Total length is now 88 bytes.
The updated touchscreen also accepts the old 84-byte snapshot and shows AC
telemetry as `0 W` and `--`. Update touchscreen before rear; older touchscreens
reject the extended snapshot. Legacy iOS, CAN and RS485 payloads are unchanged
and do not carry this new field.

Hardware checks: compare watts with Cerbo VE.Bus DC charging output while
charging; verify Bulk/Absorption/Float as applicable; verify inverter-only
operation shows zero charging watts; disconnect Cerbo and verify `0 W` / `--`.
The existing DC/DC field reads system register 855 (`/Dc/Charger/Power`), an
aggregate charger measurement, not a dedicated Orion measurement. This change
does not alter that pre-existing mapping.
