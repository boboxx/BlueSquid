#include "FirmwareUpdate.h"
#include "AppConfig.h"
#include "OtaPackage.h"
#include "WifiDefaults.h"
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <esp_ota_ops.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#include "UpdatePage.h"
#include "ControllerRelay.h"

namespace FirmwareUpdate {
namespace {
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
constexpr uint8_t kTarget = OtaPackage::kTouchscreen;
constexpr char kLabel[] = "Touchscreen";
#else
constexpr uint8_t kTarget = OtaPackage::kController;
constexpr char kLabel[] = "Controller";
#endif
WebServer server(8080);
ControllerRequest controllerRequest = nullptr;
ControllerStatus controllerStatus = nullptr;
bool relaying = false, relayAuthorized = false;
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
ControllerRelay relay;
#endif
struct CredentialRequest { OtaCredentials::Value value; uint32_t id = 0; };
OtaCredentials::Value currentCredentials;
portMUX_TYPE credentialsMux = portMUX_INITIALIZER_UNLOCKED;
QueueHandle_t credentialQueue = nullptr;
Preferences preferences;
std::atomic<uint32_t> acknowledgedCredentials{0};
char token[33]{};
bool ready = false;
size_t capacity = 0;
unsigned files = 0;
mbedtls_sha256_context sha;
bool complete = false, restartPending = false;
uint32_t restartMs = 0, lastUploadMs = 0;

void randomSecret(char (&out)[33]) {
  snprintf(out, sizeof(out), "%08lx%08lx%08lx%08lx",
      (unsigned long)esp_random(), (unsigned long)esp_random(),
      (unsigned long)esp_random(), (unsigned long)esp_random());
}
bool uploadAuthorized() {
  // Network access replaces the separate HTTP login; keep the anti-CSRF token.
  return server.header("X-BlueSquid-OTA") == token;
}
int hashStart() {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256_starts(&sha, 0);
#else
  return mbedtls_sha256_starts_ret(&sha, 0);
#endif
}
int hashWrite(const uint8_t* data, size_t size) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256_update(&sha, data, size);
#else
  return mbedtls_sha256_update_ret(&sha, data, size);
#endif
}
int hashFinish(uint8_t* digest) {
#if MBEDTLS_VERSION_NUMBER >= 0x03000000
  return mbedtls_sha256_finish(&sha, digest);
#else
  return mbedtls_sha256_finish_ret(&sha, digest);
#endif
}
struct FlashBackend {
  bool begin(size_t size) { return hashStart() == 0 && Update.begin(size, U_FLASH); }
  bool write(uint8_t* data, size_t size) {
    return hashWrite(data, size) == 0 && Update.write(data, size) == size;
  }
  bool verify(const uint8_t* expected) {
    uint8_t digest[32]{};
    return hashFinish(digest) == 0 && memcmp(digest, expected, sizeof(digest)) == 0;
  }
  bool activate() { return Update.end(); }
  void abort() { Update.abort(); }
} backend;
OtaPackage::Receiver<FlashBackend>* receiver = nullptr;
void resetUpload() {
  receiver->reset();
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
  relay.reset();
#endif
  relaying = relayAuthorized = false;
  complete = false;
  files = 0;
}
void failUpload(const char* message) {
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
  if (relaying) { relay.fail(message); return; }
#endif
  receiver->fail(message);
}
void releaseController() {
  if (controllerRequest) controllerRequest(false);
}
void upload() {
  HTTPUpload& part = server.upload();
  lastUploadMs = millis();
  if (part.status == UPLOAD_FILE_START) {
    if (restartPending) return;
    if (files == 0) {
      resetUpload();
      relaying = server.uri() == "/controller/update";
      relayAuthorized = relaying && uploadAuthorized();
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
      if (relaying && controllerStatus) {
        const auto view = controllerStatus();
        relay.configure(view.status.address, credentials());
        if (!view.ready) relay.fail("Controller is not ready. Prepare it again before uploading.");
      }
#endif
    }
    ++files;
    if (!uploadAuthorized()) { failUpload("Update authorization failed."); return; }
    if (relaying && (!controllerRequest || !controllerStatus)) {
      failUpload("Controller relay unavailable."); return;
    }
    if (!relaying && !ready) { failUpload("OTA unavailable; install the OTA layout by USB first."); return; }
    if (files != 1 || part.name != "firmware") {
      failUpload("Send exactly one firmware package."); return;
    }
  } else if (part.status == UPLOAD_FILE_WRITE) {
    if (!restartPending && files == 1 && uploadAuthorized()) {
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
      if (relaying) relay.feed(part.buf, part.currentSize);
      else
#endif
        receiver->feed(part.buf, part.currentSize);
    }
  } else if (part.status == UPLOAD_FILE_END) {
    complete = true;
  } else if (part.status == UPLOAD_FILE_ABORTED) {
    if (relayAuthorized) releaseController();
    resetUpload();
  }
}
void finish() {
  server.sendHeader("Cache-Control", "no-store");
  if (restartPending) { server.send(409, "text/plain", "Restart pending."); return; }
  if (!uploadAuthorized()) {
    resetUpload(); server.send(403, "text/plain", "Reload the update page and retry."); return;
  }
  if (files != 1 || !complete) failUpload("Send exactly one complete firmware package.");
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
  if (relaying) {
    const bool installed = relay.finish();
    server.send(installed ? 200 : 400, "text/plain", installed ?
        "Controller firmware verified and installed. Controller restarting; the touchscreen stays connected." : relay.error());
    releaseController();
    resetUpload();
    return;
  }
#endif
  if (!receiver->finish()) {
    server.send(400, "text/plain", receiver->error());
    resetUpload(); return;
  }
  releaseController();
  restartPending = true;
  restartMs = millis();
  server.send(200, "text/plain", "Firmware verified and installed. Restarting…");
  Serial.printf("OTA: %s update verified; restarting\n", kLabel);
}
void controllerNetwork(bool start) {
  if (!uploadAuthorized()) { server.send(403); return; }
  if (files || restartPending) { server.send(409, "text/plain", "Update already in progress."); return; }
  const bool accepted = controllerRequest && controllerRequest(start);
  server.send(accepted ? 202 : 409, "text/plain", accepted ?
      (start ? "Preparing Controller update connection…" : "Controller returning to Cerbo.") :
      "Controller unavailable. Check its BLE connection and install compatible firmware by USB first.");
}
void task(void*) {
  CredentialRequest request;
  bool credentialsPending = false;
  uint32_t lastSaveMs = 0;
  for (;;) {
    server.handleClient();
    if (!files && !restartPending) {
      CredentialRequest next;
      if (xQueueReceive(credentialQueue, &next, 0) == pdTRUE) {
        request = next;
        credentialsPending = true;
        lastSaveMs = millis() - 1000;
      }
      if (credentialsPending && uint32_t(millis() - lastSaveMs) >= 1000) {
        lastSaveMs = millis();
        const bool changed = !OtaCredentials::same(credentials(), request.value);
        if (!changed || preferences.putBytes("hotspot", &request.value, sizeof(request.value)) == sizeof(request.value)) {
          portENTER_CRITICAL(&credentialsMux);
          currentCredentials = request.value;
          portEXIT_CRITICAL(&credentialsMux);
          if (changed) randomSecret(token);
          acknowledgedCredentials.store(request.id);
          credentialsPending = false;
          Serial.println("OTA: hotspot credentials synchronized");
        }
      }
    }
    if (files && !restartPending && uint32_t(millis() - lastUploadMs) >= 30000) {
      if (relayAuthorized) releaseController();
      resetUpload();
    }
    if (restartPending && uint32_t(millis() - restartMs) >= 1500) ESP.restart();
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
}

bool begin(const OtaCredentials::Value* hotspot) {
  if (!preferences.begin("firmware-ota", false)) return false;
  OtaCredentials::Value initial;
  if (hotspot) initial = *hotspot;
  else if (preferences.getBytesLength("hotspot") == sizeof(initial))
    preferences.getBytes("hotspot", &initial, sizeof(initial));
  if (!OtaCredentials::valid(initial)) {
    strlcpy(initial.username, WifiDefaults::hotspotSsid, sizeof(initial.username));
    strlcpy(initial.password, WifiDefaults::hotspotPassword, sizeof(initial.password));
  }
  if (!OtaCredentials::valid(initial)) return false;
  OtaCredentials::Value saved;
  const bool matches = preferences.getBytesLength("hotspot") == sizeof(saved) &&
      preferences.getBytes("hotspot", &saved, sizeof(saved)) == sizeof(saved) &&
      OtaCredentials::same(saved, initial);
  if (!matches && preferences.putBytes("hotspot", &initial, sizeof(initial)) != sizeof(initial)) return false;
  portENTER_CRITICAL(&credentialsMux);
  currentCredentials = initial;
  portEXIT_CRITICAL(&credentialsMux);
  credentialQueue = xQueueCreate(1, sizeof(CredentialRequest));
  if (!credentialQueue) return false;
  randomSecret(token);
  const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
  const esp_partition_t* running = esp_ota_get_running_partition();
  ready = next && running && next->address != running->address;
  capacity = ready ? next->size : 0;
  static OtaPackage::Receiver<FlashBackend> uploadReceiver(backend, kTarget, capacity);
  receiver = &uploadReceiver;
  mbedtls_sha256_init(&sha);
  const char* headers[] = {"X-BlueSquid-OTA"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("X-Frame-Options", "DENY");
    server.send_P(200, "text/html", kUpdatePage);
  });
  server.on("/info", HTTP_GET, [] {
    server.sendHeader("Cache-Control", "no-store");
    String body = String("{\"device\":\"") + kLabel + "\",\"version\":\"" +
        AppConfig::kFirmwareVersion + "\",\"token\":\"" + token +
        "\",\"available\":" + (ready ? "true" : "false") +
        ",\"capacity\":" + String(capacity) + ",\"relay\":" + (controllerStatus ? "true" : "false");
    if (controllerStatus) {
      const auto view = controllerStatus();
      body += String(",\"controller\":{\"connected\":") + (view.connected ? "true" : "false") +
          ",\"supported\":" + (view.supported ? "true" : "false") +
          ",\"ready\":" + (view.ready ? "true" : "false") +
          ",\"phase\":" + String(static_cast<uint8_t>(view.status.phase)) +
          ",\"version\":\"" + String(view.status.version[0]) + "." +
          String(view.status.version[1]) + "." + String(view.status.version[2]) + "\"}";
    }
    body += "}";
    server.send(200, "application/json", body);
  });
  server.on("/update", HTTP_POST, finish, upload);
  if (controllerRequest && controllerStatus) {
    server.on("/controller/connect", HTTP_POST, [] { controllerNetwork(true); });
    server.on("/controller/disconnect", HTTP_POST, [] { controllerNetwork(false); });
    server.on("/controller/update", HTTP_POST, finish, upload);
  }
  server.onNotFound([] { server.send(404); });
  server.begin();
  if (xTaskCreate(task, "firmware-ota", 8192, nullptr, 1, nullptr) != pdPASS) {
    server.stop(); ready = false;
    vQueueDelete(credentialQueue); credentialQueue = nullptr; return false;
  }
  Serial.printf("OTA: %s port 8080; no separate page login\n", kLabel);
  if (!ready) Serial.println("OTA: two app partitions required; install via USB first");
  return true;
}
bool setHotspotCredentials(const OtaCredentials::Value& value, uint32_t requestId) {
  if (!credentialQueue || !OtaCredentials::valid(value)) return false;
  CredentialRequest request;
  request.value = value;
  request.id = requestId;
  return xQueueOverwrite(credentialQueue, &request) == pdTRUE;
}
uint32_t credentialsAcknowledgement() { return acknowledgedCredentials.load(); }
OtaCredentials::Value credentials() {
  portENTER_CRITICAL(&credentialsMux);
  const auto copy = currentCredentials;
  portEXIT_CRITICAL(&credentialsMux);
  return copy;
}
bool available() { return ready; }
void setControllerRelay(ControllerRequest request, ControllerStatus status) {
  controllerRequest = request; controllerStatus = status;
}
}
