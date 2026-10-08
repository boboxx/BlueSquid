#pragma once

#include <NimBLEDevice.h>
#include <NimBLEBondMigration.h>
#include <Preferences.h>

// Run before starting the BLE host. A persistent marker prevents the upstream
// migration helper from resetting the local IRK on subsequent boots.
inline bool prepareBleBondStorage() {
  Preferences state;
  if (!state.begin("ble-migration", false)) return false;
  if (state.getBool("v2", false)) return true;

  nvs_handle_t bonds;
  const esp_err_t opened = nvs_open("nimble_bond", NVS_READONLY, &bonds);
  bool legacy = false;
  if (opened != ESP_OK && opened != ESP_ERR_NVS_NOT_FOUND) return false;
  if (opened == ESP_OK) {
    for (unsigned slot = 1; slot <= MYNEWT_VAL(BLE_STORE_MAX_BONDS); ++slot) {
      for (const char* prefix : {"our_sec", "peer_sec"}) {
        char key[16];
        snprintf(key, sizeof(key), "%s_%u", prefix, slot);
        size_t size = 0;
        const esp_err_t err = nvs_get_blob(bonds, key, nullptr, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK ||
            (size != sizeof(NimBLEBondMigration::detail::BleStoreValueSecV1) &&
             size != sizeof(NimBLEBondMigration::detail::BleStoreValueSecCurrent))) {
          nvs_close(bonds);
          return false;  // Preserve unfamiliar records instead of dropping bonds.
        }
        legacy |= size == sizeof(NimBLEBondMigration::detail::BleStoreValueSecV1);
      }
    }
    nvs_close(bonds);
  }
  if (legacy && !NimBLEBondMigration::migrateBondStoreToCurrent()) return false;
  if (state.putBool("v2", true) != 1) return false;
  state.end();
  if (legacy) {
    Serial.println("BLE pairing storage migrated; restarting before BLE initialization");
    Serial.flush();
    ESP.restart();
    return false;
  }
  return true;
}
