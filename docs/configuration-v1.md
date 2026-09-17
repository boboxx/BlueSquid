# Configuration schema 1

Only schema_version 1 is accepted. Earlier schema parsing and legacy output-key
remapping have been removed. Export a fresh configuration before editing it.

`device` groups each device's label, icon key and colour index. Keys are
`rgbw_all`, `rgbw_1` through `rgbw_4`, and `accessory_1` through `accessory_4`.
The outputs use the same four light and four accessory keys, plus `fan`.
The group is not a physical output. All four lights use the same RGBW state
and command model. Single-channel assignments route the light intensity to the
selected R, G, B, WW or CW output; full assignments expose colour controls.

Removed metadata: touchscreen_firmware, rear_firmware_status, rear_firmware,
transport. Removed separate device_labels, device_icons and device_icon_colours.
The example is valid JSON; all accessory objects include a label property.

Each light keeps logical output_rgbw_percent [R,G,B,white], its RGB preset and
brightness, plus colour_selected, warm_white_selected and cool_white_selected.
The logical white level goes to whichever physical WW/CW channels are selected.
The RGB colour page now has a Light channels card with Colour, Warm White and
Cool White switches. All three are read together, and pending white selection
bits prevent stale feedback from swapping the switches during an update.

Pump and hot-water outputs retain the existing always-off import policy.
Accessory 4 participates in export/import. Device indices follow the group,
lights 1–4, then accessories 1–4. No old light-label or output-state migrations
are performed. The group label uses the `rgbw_all` preference key.

The uniform light model requires Controller 1.0.13 and touchscreen 1.0.19.
Stored output state from older firmware resets to safe defaults; SP630E
assignments saved using the current batch-save format retain their target indices.

## RV-C fan settings (Controller 1.0.14 / touchscreen 1.0.20)

`settings.rvc_fan` contains `enabled` (boolean), `instance` (1–250), and
`source_address` (151–159). Fresh exports include this object and imports require
it. RV-C defaults to disabled. Import applies fan configuration without commanding
fan power or speed; `outputs.fan.speed_percent` records observed state.
See [FA75 setup and wiring](rvc-fa75.md).
