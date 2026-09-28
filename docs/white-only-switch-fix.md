# White LED switches over to colour - Controller 1.0.3

Reported with touchscreen 1.0.3 and Controller 1.0.2: with both colour-page
switches off, enabling White LED briefly enables white, then white turns off
and colour turns on.

## Changes

The RGBCCT command builder previously selected static colour mode and enabled
coexistence even for white-only output. It also wrote RGB/master brightness.
White-only requests now disable coexistence, select static white mode, write
only the white component/brightness registers, and restore power after setup.
The RGBW compatibility path likewise skips RGB writes for white-only requests.
Mixed colour/white output retains coexistence.

Controller feedback now derives the active colour selection from RGB output
levels when the light is on; a coexistence flag alone cannot select colour.
No touchscreen changes are needed. Upload `main_controller` version 1.0.3;
touchscreen 1.0.3 can remain installed.

Protocol reference: [UniLED BanlanX 6xx implementation](https://github.com/monty68/uniled/blob/main/custom_components/uniled/lib/ble/banlanx_6xx.py),
including static white mode 0x02 and the separate RGB (0x52), white brightness
(0x51), and white component (0x61) commands.

## Verification

- Controller firmware builds successfully.
- `sh tests/sp630e/run.sh` passes.
- Off-to-white regression models retained red settings while powered off,
  verifies no RGB write and power-on last, then checks five feedback polls
  without a colour command being generated. Covers RGBW and RGBCCT, including CW.
- Transition tests cover off, white only, colour only, and both selections.
- Existing dimming, channel assignment, command-cache and feedback tests pass.

These tests simulate register/status behaviour. Confirm the reported sequence
on the physical SP630E after uploading; no new hardware capture was available
while implementing this fix.

## Follow-up - Controller and touchscreen 1.0.4

The reverse problem exposed an incomplete fix: RGBCCT colour-only requests
still enabled coexistence and wrote white registers. Both command builders
now isolate colour-only requests as well: static colour mode, coexistence off,
RGB writes only. White-only requests remain in static white mode with white
writes only. Both selections use coexistence; neither selection powers off.

The touchscreen now uses one callback for either LED switch. It reads both
visible checked states before creating the combined output/preset request.
Previously each callback updated its own cached selection and reused the other
cached value.

Native regression checks prohibit white writes in colour-only requests and
RGB writes in white-only requests. The former check failed against 1.0.3.
Tests also cover colour-only from off with saved white settings, white-only
from off with saved colour settings, five feedback polls in each case, and
transitions among all four combinations. The complete native suite passes.

Upload both `main_controller` and `touchscreen_controller` at 1.0.4.
Hardware verification is still required; simulated register tests do not model
all SP630E firmware side effects.

## Remaining hardware issue - diagnostic Controller 1.0.5

The user reports that enabling Colour LED from off still changes the switches
over to White LED. The supplied log shows requested RGB=100,100,100 and
WW=CW=0, with two SP630E writes. It does not identify both writes or show the
subsequent device feedback, so it does not establish the root cause.

Controller 1.0.5 adds diagnostics without changing command generation/cache
behaviour. `written packets` records opcode/payload pairs in transmission
order after successful batches. `feedback` records the first relevant response
following a command or a changed output, with acceptance/revision information,
mode, coexistence, raw component/brightness registers and decoded outputs.
Logging is outside critical sections and packet summaries are emitted after
writing, avoiding added logging delays between commands.

The Controller build passes. Upload only `main_controller` 1.0.5; touchscreen
remains 1.0.4. Reproduce once and capture SP630E log lines from before the click
through at least ten seconds after it. The remaining bug is not yet confirmed
fixed; the hardware feedback capture is the next diagnostic step.

## Captured failure and ordering correction - Controller 1.0.6

Controller 1.0.5 capture:

- Initial feedback: power=0, mode=2, coexist=0, retained warm-white component=255.
- Colour-only request sends `[0A:00 53:0101 52:FFFFFFFF 50:01]`.
- Following accepted feedback: power=1, mode=2, output=0,0,0,100,0.

The mode command was sent, so a skipped cached mode is not the explanation
for this capture. The requested mode did not survive the off-to-on sequence.
The capture alone cannot distinguish ignoring a mode write while off from
restoring the prior mode during power-on.

The planner now sends power-on first, then coexistence, mode and requested
levels. Every wake replays the complete setup regardless of cached settings.
Off remains a power-only operation; dimming while already on retains changed-
register caching. The RGBW compatibility path uses the same ordering.
The previous power-last/power-only-wake optimization is superseded.

A regression reproduces the failure under both off-state mode behaviours and
passes with the new ordering, for colour-only, white-only and combined output,
with an empty cache and a cache retained across off. Existing routing, feedback,
selection and dimming regressions pass. Retained diagnostics allow the next
hardware run to confirm colour-only feedback reports mode=1 and zero WW/CW.

Upload only `main_controller` 1.0.6. Touchscreen remains 1.0.4. Waking now takes
more writes when setup was previously cached; the change prioritizes applying
the requested mode. Hardware confirmation remains necessary, including any
brief display of the prior mode between power-on and applying the new setup.
