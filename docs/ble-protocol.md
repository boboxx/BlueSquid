# BlueSquid touchscreen BLE protocol

The rear controller is a BLE peripheral/server and the Waveshare touchscreen is
the persistent primary central/client. The legacy `7D8B100x` service remains
available for the iOS application. The optimized touchscreen service uses the
`7D8B200x` UUID family defined in `include/BlueSquidBleProtocol.h`.

## Connection lifecycle

- The touchscreen actively scans for the `BlueSquid-*` rear device, connects,
  enables encryption/bonding and subscribes to snapshot and acknowledgement
  notifications.
- The rear treats a client subscribed to the touchscreen snapshot as primary,
  so the secondary-client idle timeout does not disconnect it.
- A complete snapshot is read immediately after connecting and is notified at
  the normal one-second heartbeat interval and after accepted commands.
- Awake connection parameters target a 15-30 ms interval. While the backlight
  is asleep, the BLE connection remains present with a 100-200 ms interval.
- Losing the screen never changes rear output state. The client continuously
  scans and reconnects without user interaction.

## Packets

Commands are little-endian and contain:

```text
version:u8, sequence:u16, command:u8, target:u8, payloadLength:u8, payload
```

Acknowledgements contain:

```text
version:u8, sequence:u16, command:u8, result:u8, stateRevision:u32, target:u8
```

The 84-byte state snapshot contains all display data in one notification:
outputs, RGBW state and presets, battery/power data, charger stages, climate,
pitch/roll, calibration and firmware information. Offsets and scaling are
defined once in the shared protocol header.

RGBW gestures are optimistic and coalesced for 40 ms on the touchscreen. One
`SetRgbwState` transaction carries all four output levels, the selected RGB
colour, overall brightness and enabled-channel flags for one zone. The rear
applies it and returns the resulting revision. This prevents a colour-wheel
gesture from flooding the radio with independent channel writes.

## SP630E feedback and availability

Assigned SP630E modules keep a BLE connection and receive a status query every
2 seconds (`53 02 00 01 00 01 01`). BlueSquid subscribes to notifications on
FFE1 under FFE0/E0FF, validates complete unencrypted status packets, and reads
power, colour/white levels, RGB values, mode and coexistence settings.
Notification subscription explicitly requests an acknowledged CCCD write.
Commands and queries prefer acknowledged GATT writes even when FFE1 also
advertises write-without-response, matching UniLED's BLE transport. A GATT
acknowledgement confirms delivery only; availability still requires a valid
status notification. The
supported feedback configurations are 4CH PWM RGBW, SPI RGBW, and 5CH PWM
RGBCCT (`0x8A`) with RGB and WW wired and CW unused. For `0x8A`, logical
white means WW: static feedback reads byte 41 (effect feedback byte 51),
never the CW field. Static coexistence scales RGB and WW by the common
colour brightness. Commands enable coexistence, select static colour, and
send `0x61` with CW=0 and the requested WW component; a common master
brightness preserves independent RGB and WW levels. The white-brightness
register is explicitly synchronized using `0x51 [1, level]`; changing only
RGB brightness leaves WW unchanged on the connected PWM controller. Configuration is learned
from status before sending queued lighting commands. Unsupported,
encrypted or incomplete responses are logged and never treated as valid state.
Protocol reference: [UniLED's BanlanX 6xx implementation](https://github.com/monty68/uniled/blob/master/custom_components/uniled/lib/ble/banlanx_6xx.py).

The touchscreen queues colour and intensity changes while dragging, with a
40 ms coalescing window that is not restarted by each movement. The manager
also uses a bounded 40 ms window and forwards the latest state at most every
100 ms. The worker leaves at least 100 ms after a command batch before the next
batch; actual latency also includes BLE delivery and status-response windows.
For the RGB+WW configuration, acknowledged packet values are cached: repeated
power, coexistence and mode commands are omitted, and a typical hue change
requires only `0x52`. Initial setup sends power-on last. Failed writes and
reconnection invalidate the cache; accepted status invalidates only the individual cached packets whose
register values differ. A mode/brightness discrepancy must not replay an
unchanged power-on command. CW remains zero.

Status queries take priority over output batches every two seconds, independently
of gesture traffic. After a query the worker waits for a valid reply or up to
500 ms before sending outputs. Settings feedback is disabled throughout output
batches and enabled again for a subsequent query only when no newer command is
queued. A lost touchscreen command ACK stops blocking snapshot updates after
1.5 seconds; disconnect clears that ACK state. These bounds prevent indefinite
waiting but do not constitute a guarantee of peripheral response time.

Each assigned module has a worker task for connection setup, writes and polling.
The rear main loop continues publishing touchscreen heartbeats while a module
is missing or connecting. Clients belong to individual adapters; discovery does
not claim devices or keep pointers into scan results. Before connecting, the
worker scans for the assigned MAC and copies its full advertised address,
including the public/random address type. A saved MAC alone does not contain
that type. UI discovery is deferred while a worker owns the scanner, and its
result timer starts only once its scan has actually started. Assignment changes still
save settings and restart the rear controller.

The former ten-second idle disconnect is removed. Persistent monitoring occupies
the SP630E's single BLE connection, so BanlanX cannot connect concurrently. The
rear build permits six total BLE connections: five modules plus the touchscreen;
an additional phone connection needs a free slot.

Every fully decoded status reply refreshes module availability, even while a
newer slider/colour command is pending. Applying reported settings remains
revision-gated so old feedback cannot overwrite the newer command. UI activity
and GATT write acknowledgements alone do not refresh availability.

A disconnect immediately invalidates a module's status. A missing valid reply
expires after 8 seconds; the normal one-second rear heartbeat then updates the
screen. Failed connections retry in the background. Recovery requires a valid
status reply, not just a successful connection. No response means unavailable;
it cannot distinguish lost power from lost radio contact.

Snapshot bytes 82 and 83, previously zero/reserved, carry assigned and available
bitmasks respectively. Bits 0–1 are RGBW zones, 2–3 are the two main light zones,
and 4–7 are accessories. The snapshot stays at 84 bytes/version 1. New screens
accept old firmware's zero masks as local outputs; old screens ignore these
bytes. Both targets must be updated to display module availability.

The screen disables affected controls and displays Unavailable, including inside
the colour dialog. Unassigned local outputs retain their existing behavior.
Reported channel levels and presets update the rear state without echoing a
new lighting command. On startup/reconnection the module's state is read rather
than blindly replaying saved lighting levels. New user requests take precedence
over older feedback while queued; the next post-command report confirms the
result. Commands already queued when contact is lost are retried after recovery.

## Verification

Run host tests with `sh tests/sp630e/run.sh`, then build both PlatformIO targets.
The host tests cover packet validation, RGBW/white/off conversion, freshness
expiry and clock wrap, feedback without command echo, pending-command ordering,
shared module assignments and availability masks. Regression cases also cover
continuous 10 ms input changes without queue starvation, changed-packet selection,
failed-write retry, external power changes, reconnect cache reset and CW=0. They use synthetic protocol
fixtures; they do not replace validation against a physical module.

On hardware:

1. Flash rear and touchscreen firmware. With the assigned SP630E powered, verify
   its existing state appears and controls become available.
2. Change colour, intensity and warm white; verify the displayed settings settle
   to the reported values and the touchscreen connection stays stable.
3. Cut the SP630E supply. Within roughly 9 seconds the assigned controls should
   show Unavailable; other controls and rear heartbeats should keep working.
4. Restore the supply. Verify availability and the module's power-on state return
   automatically. Use the module's RF remote, if available, to verify externally
   changed state appears on the screen.
5. Repeat with multiple modules and verify only affected assignments become
   unavailable. Check rear logs for unsupported/malformed status messages if a
   connected module never becomes available.

For interaction validation, drag the colour wheel continuously for 20 seconds,
then do the same with intensity. Check that the strip follows during the drag,
ends at the final selection, and stays available. Repeat after switching RGB or
WW off/on, then cut and restore module power to verify availability and recovery.
The native tests do not emulate NimBLE callbacks, radio contention or the physical
SP630E; those checks require both uploaded firmwares and the connected module.

SP630E discovery/configuration/assignment writes from the touchscreen are queued
without waiting inside LVGL callbacks. The main loop performs acknowledged GATT
writes outside the display lock. Queue admission is not confirmation that an
assignment was saved: the rear validates it and restarts on acceptance. Pending
configuration requests are discarded on disconnect. Reconnection copies the BLE
address, including its type, rather than retaining a scan-owned device pointer,
and requires a valid initial snapshot before publishing connected services.
After flashing, assign a channel and verify the screen remains interactive
through the rear restart and reconnects with the saved assignment visible.

Silent-link diagnostic recovery (v3): after two acknowledged status queries and
500 ms without any FFE1 notification, the worker disables and re-enables the
subscription with acknowledged CCCD writes, then queries again. This happens
once per connection, leaves outputs/settings untouched, and does not extend the
eight-second status deadline. This is a recovery experiment for the silent
V4.0.27 module, not confirmation of the underlying cause. Capture whether the
re-arm logs are followed by status confirmation or another zero-notification
timeout; valid decoded replies remain necessary for availability.

BLE timing diagnostics log individual writes taking at least 200 ms (opcode,
duration and result), and total RGB+WW batch duration and packet count. These
measure GATT completion, not the time when the physical light reaches its target.

Display memory correction: LVGL general, font and image draw-buffer handlers
all use PSRAM-preferred allocation with matching heap_caps_free. Previously
only general buffers did so, leaving bitmap glyph allocation in the 144 KB
widget pool; the reported 28x32 A8 glyph allocation failure triggered an assert.
Hardware verification: navigate repeatedly, open colour/settings pages, and
power-cycle the rear while monitoring the touchscreen for allocation failures.

The rounded-mask allocation failure in circ_calc_aa4 uses lv_malloc rather
than draw-buffer handlers. The widget/style/mask TLSF pool is now 512 KB in
PSRAM, reserved before lv_init and released after lv_deinit. Pool allocation
failure aborts display initialization with a diagnostic instead of entering
LVGL with a null pool. Pool usage and largest free block are logged every
30 seconds while LVGL is running; repeated open/close cycles should settle
rather than consume memory indefinitely. This still needs physical validation.

Channel assignment dropdowns now use terminal labels R, G, B, CW, WW. Saved
IDs 0/1/2/3 remain R/G/B/WW; new ID 4 is CW. Display ordering maps CW before
WW without changing existing assignments or the persisted record size. Five
independent channels are supported for configuration 0x8A. Full RGBW zones
still use RGB+WW and leave CW at zero. Individual CW/WW assignments use the
separate static/effect CCT status fields and the two-component CCT command.
Both rear and touchscreen firmware need updating before assigning CW.

Colour selection uses a full hue/saturation disk: white at the centre is an
equal RGB mix, not the physical CW terminal. The separate Cool White preset
is removed; the intensity slider and WW switch remain. Channel switches submit
a complete zone state, and either enabled channel makes the main button on.
Both disabled makes it off; enabling a channel at zero intensity restores
100 percent so the requested on state has nonzero output. Main off clears both
switches and remembers the previous enabled combination for main on. Hardware
checks: centre white, saturated edge and pastel selection; RGB-only, WW-only,
both and neither; main off/on; zero-intensity recovery and offline controls.
# BLE discovery recovery (2026-09-14)

The touchscreen now accepts the legacy service UUID carried in the rear's
advertising payload, as well as the dedicated touchscreen UUID and name fallback.
Discovery therefore does not require a successful name scan response.

The rear checks advertising every five seconds and retries a stopped advertiser
when its server client limit is not reached. Failed starts are retried, without
resetting the BLE host or SP630E connections. A 30-second `BLE health` log reports
advertising, server client count and scanning. These changes address recovery
gaps; the reported overnight disconnect still needs hardware observation to
establish its initial cause. SP630E `success=1` slow-GATT warnings alone do not
establish a touchscreen connection failure.
