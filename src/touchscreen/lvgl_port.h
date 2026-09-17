#pragma once

#include "esp_display_panel.hpp"

bool lvgl_port_init(esp_panel::drivers::LCD* lcd,
                    esp_panel::drivers::Touch* touch);
bool lvgl_port_deinit();
bool lvgl_port_lock(int timeout_ms);
bool lvgl_port_unlock();
void lvgl_port_set_display_sleeping(bool sleeping);
bool lvgl_port_take_touch_activity();
bool lvgl_port_take_wake_request();
