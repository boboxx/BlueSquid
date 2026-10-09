# Power history

Settings > History shows energy generated and consumed as bars, with the
battery state of charge as a line. Day shows hourly bars; Week and Month show
daily bars for the 7 or 30 days ending on the selected day. The arrows step
back and forward one period.

## What is recorded

The Cerbo does not provide past values over Modbus (history is only in VRM),
so the touchscreen records it from the live Controller snapshot:

- Generated: solar + DC/DC + AC charger output (Cerbo DC power values).
- Consumed: generated minus battery power. Battery power is positive while
  charging, so this includes DC loads and the inverter's draw.
- State of charge: the battery monitor's SoC at the end of each slot.

Power is integrated about four times a second into 5-minute slots. Nothing is
recorded while the screen is disconnected from the Controller, the Cerbo data
is stale, or the clock is unknown. Each record stores the share of the slot
that had valid data.

Times come from the Cerbo clock (see [BLE protocol](ble-protocol.md)), so the
touchscreen needs no manual clock setting. Days and hours follow the time zone
selected on the touchscreen.

## Storage

Records are 10 bytes and appended to `/bluesquid/history/YYYY-MM.bin` (UTC
month) on the touchscreen SD card. When the files exceed 1 MB, the oldest
month is deleted, keeping about one year. Without an SD card, up to one day of
completed slots is kept in memory and written once a card is available.

Record layout (little-endian): `time:u32` (UTC slot start),
`generated:u16` and `consumed:u16` (0.1 Wh), `soc:u8` (percent, 255 unknown),
`coverage:u8` (percent of the slot with valid data). Code:
`src/touchscreen/TouchHistory.*`.

## Backup before this change

`archives/before-power-history-2026-10-09/` (excluded from Git) holds the
source tree, a Git bundle, the uncommitted diff, both targets' firmware builds,
and full flash images read from the Controller (1.0.43, first 8 MB) and the
7-inch touchscreen (1.0.62, 16 MB). The images contain private configuration
and pairing keys; keep them local.
