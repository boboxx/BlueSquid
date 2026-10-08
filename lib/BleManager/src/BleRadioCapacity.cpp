#include <esp_bt.h>
#include "Logging.h"

// Keep the ESP32-S3 radio activity budget large enough for the host.
// Connections, scanning,
// and advertising share that budget. Apply the S3 setting at initialization
// without modifying the installed framework or dependency sources.
extern "C" esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t*);
extern "C" esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t* config) {
  if (!config) return __real_esp_bt_controller_init(config);
  auto adjusted = *config;
  const unsigned previous = adjusted.ble_max_act;
  adjusted.ble_max_act = BT_CTRL_BLE_MAX_ACT_LIMIT;
  LOG_INFO("BLE", "Controller radio activities: %u -> %u; host connections: %u",
           previous, adjusted.ble_max_act, CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
  return __real_esp_bt_controller_init(&adjusted);
}
