/**
 * BlueSquid LVGL 9.5 configuration for the Waveshare ESP32-S3-Touch-LCD-7.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565

// BlueSquid owns the LVGL task, timer, and recursive mutex in lvgl_port.cpp.
#define LV_USE_OS LV_OS_NONE

// Use the portable renderer on the ESP32-S3. In particular, do not compile
// ARM Helium assembly selected by the legacy LVGL 8 configuration.
#define LV_USE_DRAW_SW 1
#define LV_USE_DRAW_SW_ASM LV_DRAW_SW_ASM_NONE
#define LV_USE_NATIVE_HELIUM_ASM 0

// Widgets, styles and software-renderer masks share this pool. Draw-buffer
// callbacks alone do not cover masks allocated through lv_malloc(). The port
// reserves this PSRAM pool before lv_init(), handling allocation failure there.
#ifndef __ASSEMBLER__
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void* bluesquid_lvgl_pool_alloc(size_t size);
#ifdef __cplusplus
}
#endif
#endif // !__ASSEMBLER__
#define LV_MEM_SIZE (512U * 1024U)
#define LV_MEM_POOL_ALLOC bluesquid_lvgl_pool_alloc
#define LV_DEF_REFR_PERIOD 30
#define LV_DPI_DEF 130
#define LV_USE_FLOAT 1

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN
#define LV_LOG_PRINTF 1

#define LV_USE_ASSERT_NULL 1
#define LV_USE_ASSERT_MALLOC 1
#define LV_USE_ASSERT_STYLE 0

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_18 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_22 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#define LV_USE_BUTTON 1
#define LV_USE_CANVAS 1
#define LV_USE_CHART 1
#define LV_USE_LABEL 1
#define LV_USE_SLIDER 1
#define LV_USE_SWITCH 1
#define LV_USE_TABVIEW 1

#define LV_USE_SYSMON 0
#define LV_USE_PERF_MONITOR 0
#define LV_USE_MEM_MONITOR 0

#define LV_BUILD_EXAMPLES 0
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
#define LV_USE_DEMO_RENDER 0
#define LV_USE_DEMO_STRESS 0

#endif  // LV_CONF_H
