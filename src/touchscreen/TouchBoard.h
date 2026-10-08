#pragma once

#include <esp_display_panel.hpp>

// Creates the display board for this touchscreen target. The caller owns it.
esp_panel::board::Board* createTouchBoard();

// Call after init(), before begin().
bool configureTouchDisplay(esp_panel::board::Board* board);
