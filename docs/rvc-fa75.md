# Dometic FA75 RV-C fan

Controller 1.0.14 and touchscreen 1.0.20 add an RV-C roof-fan client.
Backup before this work: `archives/BlueSquid_before_rvc_fan_20260917.tar.gz`.

## Controls

The Control page has a Vent fan card with a fan icon. Tap the card to toggle;
the slider sets 0–100% in ten-percent steps for the FA75's ten speeds. Turning
on recalls the last nonzero speed observed from the fan (50% before first feedback).
Zero turns the fan off. The speed slider can turn it on from Off.
The card displays Applying while awaiting fan feedback. Missing feedback produces
Command not confirmed; loss of status disables the card after 15 seconds.
Changes made at the fan update BlueSquid without sending commands back.

System Configuration → RV-C provides:

- Enabled/Disabled, default Disabled.
- Fan instance 1–250, default 1. This must match the fan's configured instance.
- Preferred Controller address 151–159, default 159. On contention the client
  tries lower free addresses in that range. Exhaustion disables transmission.
  Addresses 144–150 are excluded because the 2026 standard assigns static roles there.

The top-right check saves all three settings together and applies them without
rebooting. Settings are kept on the Controller. SD JSON export/import uses
`settings.rvc_fan` with `enabled`, `instance`, and `source_address`. Export records
the current Controller address. Import changes configuration but does not restore
fan power/speed; those output values are informational. Existing schema 1 files
need this settings object before importing.

## Wiring and commissioning

Use the Controller's configured CAN TX GPIO 37 and RX GPIO 38 to the transceiver's
TXD and RXD. Use a module with 3.3 V logic support and follow its power requirements;
a bare TJA1051 variant is not necessarily equivalent to the CAN Pal module.
Connect CAN-H and CAN-L to the fan's CAN connector, with a common signal/power ground.
The fan retains its own fused 12 V supply. Do not feed 12 V to the ESP32 logic pins
or transceiver logic supply. Confirm connector pin positions from the fan's wiring
documentation; the supplied photo shows CAN_H/CAN_L markings but is not a pinout drawing.

RV-C uses a twisted pair and 120-ohm termination at each end of the trunk. Do not
add a third terminator when joining an existing network. For a two-node bench
link, confirm termination at the Controller end and fan end.

Once wired, enable RV-C and choose the fan instance. The card stays unavailable
until a matching valid fan status arrives. Test at 10%, 50%, 100%, and Off; then
change speed at the fan and confirm the touchscreen follows. Disconnect CAN to
verify the waiting/fault state and reconnect to verify no old command is replayed.

This release controls power, manual speed, and airflow direction. Lid position,
light, temperature setpoint, and rain-sensor settings are left unchanged. Open the
lid using the fan's own control when needed. Manual speed overrides thermostatic
fan-speed operation. A successful software build is not physical FA75 validation;
that test requires the installed transceiver and fan.

## Implementation and verification

Based on [RVIA RV-C specification, February 20, 2026](https://www.rvia.org/system/files/media/file/RV-C%20Specification%20Full%20Layer%202-20-26_Final_v2.pdf),
sections 3.3, 6.46 and 7.2. Roof-fan command/status DGNs are 0x1FEA6/0x1FEA7;
speed uses half-percent units. Only matching extended eight-byte status frames
with valid instance, system state, and speed are accepted. Unchanged command
fields use all-one bits.

The normal Controller build dedicates its TWAI port to RV-C at 250 kbit/s;
BlueSquid's internal touchscreen connection remains BLE. Private BlueSquid CAN
status/command traffic is not put on this bus. The obsolete private CAN and RS-485 transport implementations have been removed.

Address probing and claiming, a control-panel diagnostic heartbeat and requested
product identification are included. The development NAME uses manufacturer
code 0 and an eFuse-derived serial. A released commercial product needs an assigned
manufacturer code and full RV-C conformance review; this is a focused fan client.

Outgoing frames use single-shot transmission with no software transmit queue.
Commands are coalesced, never restored at boot, and cleared on disconnect, timeout,
configuration change, or bus-off. Bus-off recovery repeats address claiming and
requires fresh fan status. The UI distinguishes requested state from confirmed state.

Run `sh tests/rvc/run.sh` for native tests covering encoding, status rejection,
coalescing, feedback without echo, timeouts, disabled mode, reconfiguration,
address contention/exhaustion, transmit failure, and bus-off recovery. The existing
SP630E, light-group, and configuration JSON tests also remain applicable.

## Reverse airflow

Added in Controller 1.0.15 and touchscreen 1.0.21. Backup: `archives/BlueSquid_before_fan_reverse_20260917.tar.gz`.

The Control page has a yellow **Reverse air** switch: on selects **Intake**, off selects **Exhaust**. The switch follows reported fan direction, including changes made at the fan. It is unavailable until the fan reports a valid direction.

Direction uses ROOF_FAN_COMMAND_1 byte 3 bits 0–1 (0 exhaust, 1 intake). Direction-only commands leave power, speed, lid, rain protection and temperature settings unchanged. Concurrent speed and direction changes are combined and confirmed against fan feedback; unconfirmed commands time out without replay. Physical FA75 verification remains pending.
