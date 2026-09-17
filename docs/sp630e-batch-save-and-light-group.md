# SP630E batch Save and light group

Firmware: Controller 1.0.12; touchscreen 1.0.9. Upload both targets.

Device Configuration now keeps assignment and group edits as a touchscreen
 draft. Dropdown changes do not write Controller settings or restart it. Save
submits all eight assignment rows and the group selection as one configuration.
Both ends validate the full result, including duplicate channels, full-strip
conflicts, MAC formatting and the five-device limit. This permits channel swaps
without saving conflicting intermediate states. A successful save writes one
NVS record and restarts the Controller once. The touchscreen confirms by reading
back configuration after reconnect. Failed/offline saves retain the draft for
retry; pending Save clicks are suppressed for 15 seconds.

Four checkboxes select light inputs 1–4 for Home quick access and GPIO 35.
All four are selected by default. Excluded lights are neither commanded nor
counted when deciding the next toggle state. Empty selection disables Home's
light-group action. Included lights turn on at 100%, with full strips using
physical white output, or off. Accessories and the legacy third PWM output are
outside this four-light group.

Assignments and group mask share the existing NVS blob, with one trailing byte
for the mask. Legacy assignment records are read with the default four-light
group. Existing assignment IDs and the legacy single-assignment BLE command
remain supported; legacy writes preserve the group setting. The new touchscreen
requires Controller 1.0.12 for batch Save.

Validation: native SP630E suite plus actual OutputController/GPIO group tests,
batch rejection/no-partial-application checks and excluded-light preservation.
Both firmware targets are built; device Save/reconnect and layout still require
hardware confirmation.
