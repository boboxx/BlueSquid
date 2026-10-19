# Power history

Settings > History shows energy generated and consumed as bars, with the
battery state of charge as a line. Day shows hourly bars; Week and Month show
daily bars for the 7 or 30 days ending on the selected day. The totals above
the chart cover the whole period, so in the Day view they are larger than any
single (hourly) bar. The arrows step
back and forward one period.

## What is recorded

The Cerbo does not provide past values over Modbus (history is only in VRM),
so the Controller records it from its live Cerbo readings (Controller 1.0.49
and later; earlier, each touchscreen recorded its own copy):

- Generated: solar + DC/DC + AC charger output (Cerbo DC power values).
- Consumed: generated minus battery power. Battery power is positive while
  charging, so this includes DC loads and the inverter's draw.
- State of charge: the battery monitor's SoC at the end of each slot.

Power is integrated four times a second into 5-minute slots. Nothing is
recorded while the Cerbo data is stale or its clock is unknown; whether a
touchscreen is connected or awake does not matter. Each record stores the
share of the slot that had valid data.

Times come from the Cerbo clock (see [BLE protocol](ble-protocol.md)). Days
and hours follow the time zone selected on each touchscreen: the screen lays
out the bars in local time and the Controller adds up the records for them.

## Storage

Records are 10 bytes and appended to `/bluesquid/history/YYYY-MM.bin` (UTC
month) in the Controller's flash file system (LittleFS on the 1.5 MB `spiffs`
data partition). When the files exceed 1 MB, the oldest month is deleted,
keeping about one year. A Controller factory reset does not erase them; a full
flash erase does.

A touchscreen with an SD card also keeps writing the same records there as a
backup. That copy is shown only while the Controller is offline. When the
screen connects, it uploads any SD records older than the Controller's first
recorded slot, so history recorded before the Controller took over (or lost
by erasing the Controller) is restored automatically.

Record layout (little-endian): `time:u32` (UTC slot start),
`generated:u16` and `consumed:u16` (0.1 Wh), `soc:u8` (percent, 255 unknown),
`coverage:u8` (percent of the slot with valid data). Code:
`lib/PowerHistory/` (records, charts and link encoding; `PowerHistoryStore`
on the Controller).

## Touchscreen link

The touch service has a history characteristic
(`7D8B2005-8A75-4E41-9A6A-35D0A7A18B01`, encrypted write and notify).
Messages are at most 100 bytes:

- Chart request: bars, SoC points, period start and each bar's length in
  minutes. The reply is the chart (record count, totals, Wh per bar, SoC per
  point) in numbered chunks.
- Status request: first recorded slot, newest imported record, bytes stored
  and the limit.
- Import: up to 9 records from an SD card. The reply gives the number kept.

## Backup before this change

`archives/before-power-history-2026-10-09/` (excluded from Git) holds the
source tree, a Git bundle, the uncommitted diff, both targets' firmware builds,
and full flash images read from the Controller (1.0.43, first 8 MB) and the
7-inch touchscreen (1.0.62, 16 MB). The images contain private configuration
and pairing keys; keep them local.
