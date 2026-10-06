# Display and clock

Available in Touchscreen 1.0.36. No Controller update is needed for these features.
Open **Display** from the settings menu. There is no Device card on this page.

- **Display:** daytime brightness from 5% to 100%. The configured Waveshare
  backlight is an on/off switch, so brightness uses a software dimming layer;
  this does not reduce backlight power. Sleep turns the backlight completely off.
- **Sleep:** turn off after 1, 5, 15, 30 or 60 minutes of inactivity, or Never.
  The moon button still sleeps immediately. The first touch wakes the display
  without operating the control underneath it.
- **Overnight:** enable a daily off/on schedule in 30-minute increments.
  The defaults are 22:00–07:00, disabled until enabled by the user. Equal off/on
  times disable the overnight period. During the period, a touch wakes the
  display at its overnight brightness (default 20%) for 15 seconds to 5 minutes
  after the last touch (default 30 seconds). At the end, the screen wakes at
  daytime brightness and resumes its normal inactivity timeout.

Scroll down for clock settings. The toolbar shows the local time after humidity.
Atlantic time (Moncton), including North American daylight-saving rules, is the
initial time zone. Other supported zones can be selected here.

Connect a phone or computer to the BlueSquid hotspot and open the phone remote
at `http://192.168.4.1/`. The remote sends its UTC date/time automatically and
refreshes it approximately every minute while the page is active. The display
uses its selected time zone, regardless of the phone's time zone. It continues
keeping time after the browser closes. No Internet connection is needed.

For offline use without a phone, choose hours/minutes and press **Set time**.
Manual time is a local time-of-day clock without a date, so it does not apply
automatic daylight-saving changes. Opening the remote replaces it with dated
phone time and enables daylight-saving conversion.

Brightness, sleep, overnight settings and the selected time zone survive power
cycles. The clock must be synchronized or manually set again after a restart;
this implementation does not use a battery-backed RTC. Until then, the toolbar
shows `--:--`, overnight scheduling is inactive, and normal idle sleep works.

Validation: `bash tests/display/run.sh` checks the actual clock implementation
and display scheduling, including midnight, daylight-saving offsets, disabled
schedules, exact boundaries, touch wake and timer rollover. The touchscreen
PlatformIO build is also checked. Physical brightness, touch behavior and
visual layout still need checking on the display.
