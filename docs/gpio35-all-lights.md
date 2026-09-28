# GPIO 35 all-lights switch - Controller 1.0.7

The physical button previously called the two-PWM-slot control and used its
brightness recall values. It did not include the two original RGB assignments
in either its toggle decision or output commands.

The debounced, active-low momentary input remains GPIO 35. A press now:

- Turns all light outputs off when any light is on.
- Turns all lights on at 100% when all lights are off.
- Uses white output for full strips, retaining the saved warm/cool selection
  and defaulting to warm white if there is none.
- Includes single-channel assignments, full-strip replacements for PWM slots,
  and the legacy third PWM light output. Full-strip replacements are handled once.
- Leaves accessory relays and fan output unchanged.

The existing two-PWM-slot touchscreen/protocol action retains its behaviour;
the physical button has its own all-lights action. Each press logs
`GPIO 35 pressed: all lights ON at 100%` or `GPIO 35 pressed: all lights OFF`.

Validation: `sh tests/all_lights/run.sh` compiles the real OutputController,
LightSwitchManager and EventManager with simulated GPIO/PWM/persistence. It
checks debounce and hold/release, 100% instead of recalled dim levels, RGB-only
and extra-strip-only toggle decisions, preserved white-channel masks,
unaffected accessories, and operation of external lights without a PCA9685.
The existing SP630E regression suite also passes.

Upload `main_controller` 1.0.7. Touchscreen remains 1.0.4. Physical switch and
light response still need confirmation after flashing.

Unassigned RGB slots with no local GPIO output are excluded from the physical
all-lights action and toggle decision. A regression covers one assigned RGB
light and stale output state left in an unassigned slot, including turning the
assigned light off through the touchscreen before pressing GPIO 35 again.

## Shared Home group - Controller 1.0.8 / touchscreen 1.0.6

Supersedes the separate-action behavior above: Home quick access and GPIO 35
now invoke `setAllLightsEnabled`. BLE, CAN and bench RS-485 SetAllLights
handlers all use the same action: off, or all lights at 100% with full strips
using their white output.

The Home group indicator includes assigned RGB outputs and all three legacy
PWM outputs. Stale unassigned RGB state is excluded. The button requires the
Controller connection; an unavailable individual assignment no longer disables
the entire group. Individual cards follow the Controller snapshot, and older
pending per-light edits are cleared when a group command is sent.

Native regression coverage includes RGB-only Home state, all four assignment
positions, legacy PWM state, excluded accessories, and alternating Home action
and GPIO presses with no PCA9685. Upload both firmware targets. Electrical GPIO
input and physical SP630E response still require confirmation on the devices.
