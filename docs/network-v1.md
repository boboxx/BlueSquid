# Version 1 network setup

## Build-time Wi-Fi defaults (1.0.1)

Copy `platformio.local.example.ini` to `platformio.local.ini`. Set `cerbo_ssid`
and `cerbo_password` in its `[bluesquid_wifi]` section; set `hotspot_ssid` and
`hotspot_password` for the phone hotspot. The local file is excluded from Git.
Without local overrides, Cerbo credentials are empty and can be entered in settings.

Saved device settings override build-time defaults, including after uploading
new firmware. To change an already configured device, use its touchscreen
settings page. Each target is now version 1.0.1. Credentials entered here are
included in the firmware and project backups.

Controller and Touchscreen are independently versioned at 1.0.0 for this
network migration. The build targets are `main_controller` and
`touchscreen_controller`.

## Connections

- Controller joins the Cerbo's hotspot as a Wi-Fi station. It no longer
  creates a hotspot. BLE lighting and touchscreen links remain independent.
- Touchscreen creates the `BlueSquid` phone hotspot. Its initial password is
  `1234567890`; change both under System Configuration → System hotspot.
- The phone remote is served by Touchscreen at `http://192.168.4.1/` (use the
  address displayed on the System hotspot page if different). It does not
  require internet or cloud services. This hotspot is not an internet router.

## Migration

1. Flash both devices. The verified pre-change project is in
   `archives/before-network-change-20260914-151147/BlueSquid_v1.zip` with SHA256.
   That archive excludes earlier archives and hardware NVS settings.
2. In System Configuration → Victron Cerbo GX, enter the actual Cerbo hotspot
   SSID/password and the VE.Bus unit ID. Credentials for the former Controller
   hotspot are deliberately not migrated into the new station settings.
3. Keep Modbus TCP enabled on Cerbo. The Controller uses the DHCP gateway as
   its Cerbo address instead of scanning clients on its former hotspot.
4. Join BlueSquid from the phone and open the address above. Configuration
   changes restart the touchscreen hotspot and require the phone to reconnect.

Cerbo hotspot reference:
https://www.victronenergy.com/media/pg/Cerbo_GX/en/accessing-the-gx-device.html

## Behaviour and limits

Wi-Fi connection attempts are asynchronous and retried every 15 seconds while
disconnected. Cerbo Modbus polling requires the station link and clears stale
telemetry after loss. Being connected to the Controller over BLE does not imply
that Cerbo telemetry is available. System hotspot credentials live in the
touchscreen's own NVS namespace. Neither Wi-Fi password is included in the
existing SD configuration export.

The initial web remote supports two pot lights, two RGB/WW zones, the four
accessory outputs, inverter and charger controls, and basic power readings.
It uses device labels and the existing BLE command path. Slider/colour changes
are submitted on release; status is polled each second. A sent command is not
a physical-state confirmation; subsequent Controller feedback supplies state.
Output availability is checked on the server as well as in the page.
The WPA-protected hotspot is the access boundary. A per-boot token in a custom
HTTP header prevents cross-origin form submissions; no CORS access is enabled.

Visible touchscreen keyboards hide the bottom navigation and extend their
settings page to 480 pixels. The page back button remains accessible. Keyboard
discovery walks the visible LVGL tree, so it applies to new keyboard pages too.

## Hardware validation

Verify correct and incorrect Cerbo credentials, Cerbo power loss/recovery,
Controller reboot, hotspot persistence across touchscreen reboot, and phone
reconnection after hotspot changes. Exercise lighting while Modbus is offline
and while the phone polls. Confirm RGB and WW remain independent and unavailable
outputs reject web commands. Check every keyboard page and return navigation.
Builds and host light-regression tests do not establish RF coexistence or
physical touchscreen behaviour.
