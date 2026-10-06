#pragma once

#include <lvgl.h>

// Font Awesome's lightbulb code point. LVGL's bundled symbol subset does not
// include it, so BlueSquid supplies the two small bitmap glyphs below.
#define BLUESQUID_SYMBOL_FLAMES "\xEE\x80\x81"
#define BLUESQUID_SYMBOL_SPIGOT "\xEE\x80\x80"
#define BLUESQUID_SYMBOL_FAN "\xEF\xA1\xA3"
#define BLUESQUID_SYMBOL_LIGHTBULB "\xEF\x83\xAB"
#define BLUESQUID_SYMBOL_SUN "\xEF\x86\x85"
#define BLUESQUID_SYMBOL_POWER_CORD "\xEF\x87\xA6"
#define BLUESQUID_SYMBOL_THERMOMETER "\xEF\x8B\x87"
#define BLUESQUID_SYMBOL_HUMIDITY "\xEF\x9D\xB3"

LV_FONT_DECLARE(bluesquid_font_lightbulb_14)
LV_FONT_DECLARE(bluesquid_font_lightbulb_28)
LV_FONT_DECLARE(bluesquid_font_sun_24)
LV_FONT_DECLARE(bluesquid_font_climate_24)
