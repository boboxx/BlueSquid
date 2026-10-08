#include "TouchBoard.h"

#include <Arduino.h>

#if BLUESQUID_TOUCHSCREEN_10IN
#include <variant>

#include "board/esp_panel_board_default_config.hpp"
#include "driver/i2c.h"

namespace {
// GT9271 touch on the board's shared I2C bus. Waveshare leaves INT and RST
// unconnected, so the controller may answer at either GT911 address.
constexpr i2c_port_t kTouchPort = I2C_NUM_0;
constexpr int kTouchSdaPin = 7;
constexpr int kTouchSclPin = 8;
constexpr uint8_t kTouchAddresses[] = {0x5D, 0x14};

// Uses the legacy I2C driver, as ESP32_Display_Panel does: linking Arduino's
// Wire (the new driver) beside it aborts at boot on ESP-IDF 5.5.
uint8_t probeTouchAddress() {
  i2c_config_t config{};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = kTouchSdaPin;
  config.scl_io_num = kTouchSclPin;
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = 100000;
  if (i2c_param_config(kTouchPort, &config) != ESP_OK ||
      i2c_driver_install(kTouchPort, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
    return 0;
  }
  uint8_t found = 0;
  for (const uint8_t address : kTouchAddresses) {
    i2c_cmd_handle_t command = i2c_cmd_link_create();
    i2c_master_start(command);
    i2c_master_write_byte(command, (address << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(command);
    const esp_err_t result =
        i2c_master_cmd_begin(kTouchPort, command, pdMS_TO_TICKS(50));
    i2c_cmd_link_delete(command);
    if (result == ESP_OK) {
      found = address;
      break;
    }
  }
  // Release the port; the display library installs its own driver on it.
  i2c_driver_delete(kTouchPort);
  return found;
}
}  // namespace

esp_panel::board::Board* createTouchBoard() {
  esp_panel::board::BoardConfig config = ESP_PANEL_BOARD_DEFAULT_CONFIG;
  const uint8_t address = probeTouchAddress();
  if (address == 0) {
    Serial.println("Touch controller not found at 0x5D or 0x14; using 0x5D");
  } else if (config.touch.has_value()) {
    auto* bus = std::get_if<esp_panel::drivers::BusI2C::Config>(
        &config.touch->bus_config);
    if (bus != nullptr) bus->control_panel.dev_addr = address;
    Serial.printf("Touch controller found at 0x%02X\n", address);
  }
  return new esp_panel::board::Board(config);
}
#else
esp_panel::board::Board* createTouchBoard() {
  return new esp_panel::board::Board();
}
#endif
