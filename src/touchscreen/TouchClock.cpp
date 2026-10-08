#include "TouchClock.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_timer.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>

namespace TouchClock {
namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
int64_t base = 0;
bool valid = false, dated = false;
unsigned selectedZone = 0;
Preferences prefs;
const char* zones[] = {"AST4ADT,M3.2.0,M11.1.0", "EST5EDT,M3.2.0,M11.1.0",
  "CST6CDT,M3.2.0,M11.1.0", "MST7MDT,M3.2.0,M11.1.0",
  "PST8PDT,M3.2.0,M11.1.0", "NST3:30NDT,M3.2.0,M11.1.0", "UTC0"};
int64_t seconds() { return esp_timer_get_time() / 1000000; }
}
void setZone(unsigned zone) {
  if (zone >= sizeof(zones) / sizeof(zones[0])) zone = 0;
  selectedZone = zone;
  setenv("TZ", zones[zone], 1); tzset();
  if (!prefs.isKey("zone") || prefs.getUInt("zone") != zone) prefs.putUInt("zone", zone);
}
unsigned zone() { return selectedZone; }
void begin() { prefs.begin("display-clock", false); setZone(prefs.getUInt("zone", 0)); }
Reading read() {
  portENTER_CRITICAL(&mux);
  const bool known = valid, calendar = dated;
  const int64_t value = base + seconds();
  portEXIT_CRITICAL(&mux);
  Reading out{known, 0, "--:--"};
  if (!known) return out;
  if (calendar) {
    const time_t epoch = value; struct tm local{}; localtime_r(&epoch, &local);
    out.minute = local.tm_hour * 60 + local.tm_min;
  } else out.minute = (value / 60) % 1440;
  snprintf(out.text, sizeof(out.text), "%02u:%02u", out.minute / 60, out.minute % 60);
  return out;
}
void timestamp(char* out, size_t size) {
  if (out == nullptr || size == 0) return;
  portENTER_CRITICAL(&mux);
  const bool known = valid, calendar = dated;
  const int64_t value = base + seconds();
  portEXIT_CRITICAL(&mux);
  out[0] = '\0';
  if (!known) return;
  if (calendar) {
    const time_t epoch = value; struct tm local{}; localtime_r(&epoch, &local);
    strftime(out, size, "%Y-%m-%d %H:%M:%S", &local);
  } else {
    const unsigned day = static_cast<unsigned>(value % 86400);
    snprintf(out, size, "%02u:%02u:%02u", day / 3600, day / 60 % 60, day % 60);
  }
}
bool sync(int64_t epoch) {
  if (epoch < 1704067200LL || epoch > 4102444799LL) return false;
  portENTER_CRITICAL(&mux); base = epoch - seconds(); valid = dated = true; portEXIT_CRITICAL(&mux);
  return true;
}
bool setTime(unsigned hour, unsigned minute) {
  if (hour > 23 || minute > 59) return false;
  portENTER_CRITICAL(&mux);
  base = int64_t(hour * 3600 + minute * 60) - seconds(); valid = true; dated = false;
  portEXIT_CRITICAL(&mux);
  return true;
}
}
