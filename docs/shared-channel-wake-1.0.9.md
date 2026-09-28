# Shared-channel wake - Controller 1.0.9

With a relay on R and a light on G of one SP630E, the old all-off command
powered down the peripheral without clearing its stored green level. The next
power-on could restore green before the new red-only packet arrived.

The command planner now clears RGB components/master brightness and white
brightness while the device is awake, then sends power-off. The cache drops
those old register values after successful shutdown. Wake still applies power
before mode/levels, preserving the earlier mode-restoration fix. Logical colour
and brightness presets remain on the Controller and touchscreen.

A regression models green on, all off, then red relay on, and checks green after
every intermediate wake packet, not just the final state. Existing SP630E and
GPIO/group tests pass. Only Controller firmware changes; touchscreen stays 1.0.7.

Hardware validation: after flashing, turn the green light on and off, then
turn on the red relay. Green should stay off throughout. This addresses normal
shutdown commanded by BlueSquid; startup with old peripheral state or changes
made by another app still require observation on hardware.

## Independent intensity - Controller 1.0.10

The follow-up report has G already on and dimmed while the relay on R toggles.
The previous encoder normalized channel components against the largest active
level. G at 34% used component 255/master 86; adding a 100% relay changed it to
component 86/master 255. Although the final product is equivalent, a peripheral
that applies these fields separately can momentarily drive G at full intensity.

Individual assignments now use a fixed 100% master and absolute per-channel
components. Thus G at 34% stays component 86/master 255 as the relay toggles.
Full-strip assignments retain their existing colour/white encoding. The policy
is derived from assignments, with no wire protocol or saved-setting migration.
The earlier clear-before-power-off behavior remains.

Regression coverage routes both original RGB and legacy PWM light assignments
through the real assignment manager with R as an accessory, for five intensity
levels and repeated relay toggles. Both RGBW and RGBCCT packet builders must keep
the green component and master unchanged. SP630E and all-lights tests pass.
Upload main_controller 1.0.10; touchscreen stays 1.0.7. Physical transient behavior
still needs confirmation; after uploading, set G to 34%, then toggle R repeatedly.

## Shared-output idle - Controller 1.0.11

For independent assignments, all-off now means zero channel components with the
SP630E still enabled, in static combined mode and fixed master brightness.
This avoids repeating the global power-on sequence and mode changes when a
relay starts from an otherwise idle device. Full-strip assignments retain their
existing power-off behavior. RGBW compatibility devices now also use the
acknowledged command cache, reconciled against feedback, as RGBCCT devices do.

Tests check repeated all-off/relay-on transitions, requiring exactly one RGB
packet and no power or mode command. Both RGBW and RGBCCT feedback must report
zero on the other channels; previous independent intensity tests also pass.
Initial power-up/reconnection or an external app's power-off can still require
one physical wake. Hardware testing must distinguish that first wake from
normal repeated toggles. Upload main_controller 1.0.11; touchscreen stays 1.0.7.
