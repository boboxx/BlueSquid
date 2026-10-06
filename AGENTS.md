# Firmware versions

The user wants firmware versions incremented as changes are delivered.
For each completed batch of firmware changes, increment the patch version for
each affected device in `include/AppConfig.h` once. The touchscreen branch is
selected by `BLUESQUID_TOUCHSCREEN_FIRMWARE`; the other branch is Controller.
Keep `kFirmwareVersion` and the numeric major,
minor and patch constants consistent. Do not increment for build retries,
documentation-only changes or tests alone.

The two devices have independent versions; do not bump an unaffected device.
Report each affected version and which targets need uploading. Installed devices retain their
previous version until flashed; different displayed versions are expected
when only one target needs an update. Use "Controller" in user-facing labels
and diagnostics, rather than "Rear controller". Keep existing build environment
names and wire/export identifiers compatible unless a migration is requested.

# Firmware uploads

After completing firmware changes and a successful build, automatically flash
each affected connected device. The user has authorized this workflow; no
additional confirmation is needed for routine uploads. Report which devices
were flashed and any device that could not be reached.
