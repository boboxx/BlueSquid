#pragma once

// Native-resolution layout for the 10.1-inch screen.
//
// The UI is laid out in the 7-inch screen's 800x480 units. On the 10.1-inch
// screen LVGL renders at the panel's native 1280x800, so every pixel value
// passed to LVGL is multiplied by 8/5 here and every pixel value read back is
// divided again. Fonts map to the nearest larger Montserrat size, so text is
// rasterized at full resolution instead of being stretched. Include after
// lvgl.h, in the UI source only; the 7-inch build does not include it.

#include <lvgl.h>

namespace TouchScale {

constexpr int32_t kNumerator = 8;
constexpr int32_t kDenominator = 5;
// 480 units x 8/5 = 768 pixels; the screen pads the remaining 32 rows.
constexpr int32_t kTopPadding = 10;

inline bool special(int32_t value) {
  return LV_COORD_IS_SPEC(value) || value >= LV_RADIUS_CIRCLE;
}
inline int32_t up(int32_t value) {
  if (special(value)) return value;
  const int32_t scaled = value * kNumerator;
  return (scaled >= 0 ? scaled + kDenominator / 2 : scaled - kDenominator / 2) / kDenominator;
}
inline int32_t down(int32_t value) {
  if (special(value)) return value;
  const int32_t scaled = value * kDenominator;
  return (scaled >= 0 ? scaled + kNumerator / 2 : scaled - kNumerator / 2) / kNumerator;
}
inline void down(lv_area_t* area) {
  area->x1 = down(area->x1);
  area->y1 = down(area->y1);
  area->x2 = down(area->x2);
  area->y2 = down(area->y2);
}
inline void getCoords(const lv_obj_t* object, lv_area_t* area) {
  lv_obj_get_coords(object, area);
  down(area);
}
inline void getPoint(lv_indev_t* indev, lv_point_t* point) {
  lv_indev_get_point(indev, point);
  point->x = down(point->x);
  point->y = down(point->y);
}

// Montserrat sizes x 8/5, rounded to an available size.
inline const lv_font_t& kFont12 = lv_font_montserrat_20;
inline const lv_font_t& kFont14 = lv_font_montserrat_22;
inline const lv_font_t& kFont16 = lv_font_montserrat_26;
inline const lv_font_t& kFont18 = lv_font_montserrat_28;
inline const lv_font_t& kFont20 = lv_font_montserrat_32;
inline const lv_font_t& kFont22 = lv_font_montserrat_36;
inline const lv_font_t& kFont24 = lv_font_montserrat_38;
inline const lv_font_t& kFont28 = lv_font_montserrat_44;

// Icon glyphs exist only as small bitmaps, so these resample them by 8/5.
const lv_font_t* scaledFont(const lv_font_t* base);

}  // namespace TouchScale

// Object geometry.
#define lv_obj_set_pos(o, x, y) lv_obj_set_pos(o, TouchScale::up(x), TouchScale::up(y))
#define lv_obj_set_size(o, w, h) lv_obj_set_size(o, TouchScale::up(w), TouchScale::up(h))
#define lv_obj_set_width(o, w) lv_obj_set_width(o, TouchScale::up(w))
#define lv_obj_set_height(o, h) lv_obj_set_height(o, TouchScale::up(h))
#define lv_obj_set_x(o, x) lv_obj_set_x(o, TouchScale::up(x))
#define lv_obj_set_y(o, y) lv_obj_set_y(o, TouchScale::up(y))
#define lv_obj_align(o, a, x, y) lv_obj_align(o, a, TouchScale::up(x), TouchScale::up(y))
#define lv_obj_align_to(o, b, a, x, y) \
  lv_obj_align_to(o, b, a, TouchScale::up(x), TouchScale::up(y))
#define lv_obj_set_ext_click_area(o, s) lv_obj_set_ext_click_area(o, TouchScale::up(s))
#define lv_obj_scroll_to_y(o, y, a) lv_obj_scroll_to_y(o, TouchScale::up(y), a)
#define lv_tabview_set_tab_bar_size(t, s) lv_tabview_set_tab_bar_size(t, TouchScale::up(s))
#define lv_obj_get_width(o) TouchScale::down(lv_obj_get_width(o))
#define lv_obj_get_height(o) TouchScale::down(lv_obj_get_height(o))
#define lv_obj_get_coords(o, a) TouchScale::getCoords(o, a)
#define lv_indev_get_point(i, p) TouchScale::getPoint(i, p)

// Style values in pixels.
#define BLUESQUID_SCALED_STYLE(name, o, v, s) \
  lv_obj_set_style_##name(o, TouchScale::up(v), s)
#define lv_obj_set_style_radius(o, v, s) BLUESQUID_SCALED_STYLE(radius, o, v, s)
#define lv_obj_set_style_border_width(o, v, s) BLUESQUID_SCALED_STYLE(border_width, o, v, s)
#define lv_obj_set_style_shadow_width(o, v, s) BLUESQUID_SCALED_STYLE(shadow_width, o, v, s)
#define lv_obj_set_style_outline_width(o, v, s) BLUESQUID_SCALED_STYLE(outline_width, o, v, s)
#define lv_obj_set_style_outline_pad(o, v, s) BLUESQUID_SCALED_STYLE(outline_pad, o, v, s)
#define lv_obj_set_style_line_width(o, v, s) BLUESQUID_SCALED_STYLE(line_width, o, v, s)
#define lv_obj_set_style_pad_all(o, v, s) BLUESQUID_SCALED_STYLE(pad_all, o, v, s)
#define lv_obj_set_style_pad_top(o, v, s) BLUESQUID_SCALED_STYLE(pad_top, o, v, s)
#define lv_obj_set_style_pad_bottom(o, v, s) BLUESQUID_SCALED_STYLE(pad_bottom, o, v, s)
#define lv_obj_set_style_pad_left(o, v, s) BLUESQUID_SCALED_STYLE(pad_left, o, v, s)
#define lv_obj_set_style_pad_right(o, v, s) BLUESQUID_SCALED_STYLE(pad_right, o, v, s)
#define lv_obj_set_style_pad_row(o, v, s) BLUESQUID_SCALED_STYLE(pad_row, o, v, s)
#define lv_obj_set_style_pad_column(o, v, s) BLUESQUID_SCALED_STYLE(pad_column, o, v, s)
#define lv_obj_set_style_width(o, v, s) BLUESQUID_SCALED_STYLE(width, o, v, s)
#define lv_obj_set_style_height(o, v, s) BLUESQUID_SCALED_STYLE(height, o, v, s)
#define lv_obj_set_style_transform_width(o, v, s) BLUESQUID_SCALED_STYLE(transform_width, o, v, s)
#define lv_obj_set_style_transform_height(o, v, s) BLUESQUID_SCALED_STYLE(transform_height, o, v, s)
#define lv_obj_set_style_text_line_space(o, v, s) BLUESQUID_SCALED_STYLE(text_line_space, o, v, s)
#define lv_obj_get_style_radius(o, p) TouchScale::down(lv_obj_get_style_radius(o, p))
#define lv_obj_get_style_pad_row(o, p) TouchScale::down(lv_obj_get_style_pad_row(o, p))
#define lv_obj_get_style_pad_column(o, p) TouchScale::down(lv_obj_get_style_pad_column(o, p))
#define lv_obj_get_style_space_top(o, p) TouchScale::down(lv_obj_get_style_space_top(o, p))
#define lv_obj_get_style_space_bottom(o, p) TouchScale::down(lv_obj_get_style_space_bottom(o, p))
#define lv_obj_get_style_space_left(o, p) TouchScale::down(lv_obj_get_style_space_left(o, p))
#define lv_obj_get_style_space_right(o, p) TouchScale::down(lv_obj_get_style_space_right(o, p))

// Fonts.
#define lv_font_montserrat_12 TouchScale::kFont12
#define lv_font_montserrat_14 TouchScale::kFont14
#define lv_font_montserrat_16 TouchScale::kFont16
#define lv_font_montserrat_18 TouchScale::kFont18
#define lv_font_montserrat_20 TouchScale::kFont20
#define lv_font_montserrat_22 TouchScale::kFont22
#define lv_font_montserrat_24 TouchScale::kFont24
#define lv_font_montserrat_28 TouchScale::kFont28

// Icon fonts (LightbulbFont.h), resampled to the native scale.
#define bluesquid_font_lightbulb_14 (*TouchScale::scaledFont(&::bluesquid_font_lightbulb_14))
#define bluesquid_font_lightbulb_28 (*TouchScale::scaledFont(&::bluesquid_font_lightbulb_28))
#define bluesquid_font_sun_24 (*TouchScale::scaledFont(&::bluesquid_font_sun_24))
#define bluesquid_font_climate_24 (*TouchScale::scaledFont(&::bluesquid_font_climate_24))
