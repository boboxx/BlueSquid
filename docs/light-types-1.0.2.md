# Configurable lights - firmware 1.0.2

Upload both `main_controller` (Controller) and `touchscreen_controller`.
Installed devices keep their previous versions until flashed.

## Controls

Under System Configuration → Device configuration → Bluetooth Module,
each of the four light assignment dropdowns offers a discovered/saved SP630E
with either **Full RGBCWWW** or **Single R/G/B/CW/WW**.

- Full strips have on/off and brightness on the main card. The colour button
  opens the colour wheel, colour-LED switch, white-LED switch, and warm/cool/both
  white selection. Brightness is shared by the selected LEDs.
- Single-channel assignments have on/off and brightness, without a colour button.
- A full strip owns its SP630E. Single-channel assignments may share one SP630E
  when each uses a different channel. Conflicting assignments are rejected.
- Assignments are saved on the Controller. Accepted changes restart it; the
  touchscreen refreshes the confirmed assignments after reconnecting.
- Existing assignments retain their defaults. CW requires a controller/strip
  with the cool-white channel; RGBW strips can continue using warm white.

## Compatibility

Existing assignment IDs, BLE command IDs and the first 88 snapshot bytes are
preserved. The snapshot appends two additional RGBW output/preset records.
Saved Controller state version 2 is validated and migrated to version 3.
SD exports use schema 5 for all four RGB presets and cool-white selection;
schema 4 imports remain supported. As before, SD preset exports do not replace
SP630E device assignments.

The pre-change project backup is
`archives/BlueSquid_before_light_types_20260915.tar.gz`; build caches and older
archives are excluded.

## Validation

Both PlatformIO targets build. `sh tests/sp630e/run.sh` covers existing protocol
behaviour plus shared single-channel assignments in the former RGBW slots,
full strips in both former PWM slots, warm/cool output routing, off output,
availability bits, dropdown channel mapping, and cool-white feedback brightness.

Physical touchscreen gestures, SP630E output wiring, reboot persistence and SD
round trips still need verification on the devices after upload.

## Touchscreen 1.0.3 update

The colour page now places Colour LED and White LED switches on the right of
the wheel. The white-temperature dropdown has been removed. White LED retains
the saved warm/cool channel mask (warm white by default); Colour LED and White
LED remain independent and share the main card brightness. Choosing a colour
on the wheel enables Colour LED while preserving the White LED selection.
Only `touchscreen_controller` needs uploading for this update; Controller
firmware remains 1.0.2.

## Touchscreen 1.0.5 update

Colour selection now requires both a saved SP630E device address and the full-
strip type. Unassigned slots no longer show colour controls just because they
retain a default or previous full-strip channel value. This applies to all four
light cards and closes the colour page when its assignment is removed.
Single-channel and unassigned cards retain the basic on/off/brightness layout.

Upload `touchscreen_controller` 1.0.5 for this display correction. Controller
1.0.7 contains the separate GPIO 35 all-lights correction.

## Uniform light cards - touchscreen 1.0.7

All four light slots use the RGB card layout: title, percentage (including 0%),
and intensity slider. Removed the PWM-only Brightness caption and normal
On/Off text; connection/availability messages remain visible when needed.
Only a nonempty full-strip assignment displays the colour button. Unassigned
original RGB slots also use intensity-only output selection, even if their
saved channel type was full strip.

Default labels are RGB Light 1–4 (assignment target order); the old PWM slots
are RGB Light 3 and 4. All PWM lights becomes All lights. Factory labels loaded
from preferences migrate to these names; custom labels and persistence/wire
identifiers remain unchanged. Single-channel assignments continue to map
intensity to their selected physical channel; full assignments use RGBW state.
Controller remains 1.0.8; upload touchscreen_controller 1.0.7.

## RGB-only assignments (Controller 1.0.17 / touchscreen 1.0.27)

The SP630E light dropdown includes **Full RGB**, alongside **Full RGBCWWW**
and the individual channels. RGB-only lights retain the colour wheel, Colour
switch, on/off button and brightness slider. Warm White and Cool White switches
are hidden on the touchscreen and web remote.

RGB-only assignments use ID 254 and reserve the whole SP630E, like full-strip
assignments (ID 255). Physical WW and CW commands are always zero. Group and
scene requests for white use the RGB channels to produce white instead.
Both firmware targets must be updated before selecting this type.

Controller 1.0.18 additionally accepts SP630E hardware configuration 0x85
(three-channel PWM RGB: white channels unused). It decodes RGB feedback and
sends power, static-colour mode and RGB brightness commands without white or
coexistence commands. The earlier Full RGB assignment alone did not add this
hardware-mode support.
