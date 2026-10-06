#include "ControllerRelay.h"
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <base64.h>

ControllerRelay::ControllerRelay() : receiver_(backend_, OtaPackage::kController, UINT32_MAX) {
  mbedtls_sha256_init(&backend_.sha);
}
void ControllerRelay::reset() {
  receiver_.reset();
  backend_.abort();
  backend_.failure = "";
  headerBytes_ = 0;
}
void ControllerRelay::configure(const uint8_t* address, const OtaCredentials::Value& login) {
  reset();
  backend_.address = IPAddress(address[0], address[1], address[2], address[3]);
  backend_.login = login;
}
void ControllerRelay::feed(uint8_t* data, size_t size) {
  if (receiver_.error()[0]) return;
  if (headerBytes_ < OtaPackage::kHeaderSize) {
    const size_t count = min(size, OtaPackage::kHeaderSize - headerBytes_);
    memcpy(backend_.header + headerBytes_, data, count);
    headerBytes_ += count; data += count; size -= count;
    if (headerBytes_ != OtaPackage::kHeaderSize) return;
    receiver_.feed(backend_.header, sizeof(backend_.header));
  }
  receiver_.feed(data, size);
}
const char* ControllerRelay::error() const {
  return backend_.failure.isEmpty() ? receiver_.error() : backend_.failure.c_str();
}
bool ControllerRelay::Backend::send(const uint8_t* data, size_t size) {
  const uint32_t started = millis();
  while (size && uint32_t(millis() - started) < 10000) {
    const size_t written = client.write(data, size);
    if (!written) { failure = "Controller transfer interrupted. Reconnect and retry."; return false; }
    data += written; size -= written;
  }
  return size == 0;
}
bool ControllerRelay::Backend::begin(size_t size) {
  const auto local = WiFi.softAPIP();
  const auto mask = WiFi.softAPSubnetMask();
  // Never relay to an address outside our own hotspot or to the touchscreen.
  if (address == local || address[3] == 0 || address[3] == 255) return false;
  for (unsigned i = 0; i < 4; ++i)
    if ((address[i] & mask[i]) != (local[i] & mask[i])) return false;
  HTTPClient http;
  WiFiClient infoClient;
  http.setConnectTimeout(3000);
  http.setTimeout(5000);
  if (!http.begin(infoClient, "http://" + address.toString() + ":8080/info")) return false;
  // Keep credentials on relay requests so older Controller firmware can still be updated.
  http.setAuthorization(login.username, login.password);
  const int response = http.GET();
  if (response != 200 || http.getSize() < 0 || http.getSize() > 1024) {
    failure = "Controller updater unavailable. Reconnect and check hotspot login synchronization.";
    http.end(); return false;
  }
  JsonDocument info;
  const auto parse = deserializeJson(info, http.getString());
  http.end();
  const String token = info["token"] | "";
  const uint32_t maximum = info["capacity"] | 0U;
  if (parse || info["device"] != "Controller" || !info["available"].as<bool>() ||
      token.length() != 32 || !maximum || maximum > 16UL * 1024UL * 1024UL || size > maximum) {
    failure = "Controller rejected package size or does not have the OTA layout."; return false;
  }
  if (mbedtls_sha256_starts(&sha, 0) != 0 || !client.connect(address, 8080, 3000)) return false;
  client.setTimeout(5000);
  boundary = "BlueSquid" + String(esp_random(), HEX) + String(esp_random(), HEX);
  const String prefix = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"firmware\"; filename=\"Controller.bsfw\"\r\nContent-Type: application/octet-stream\r\n\r\n";
  const String suffix = "\r\n--" + boundary + "--\r\n";
  const size_t length = prefix.length() + sizeof(header) + size + suffix.length();
  const String authorization = base64::encode(String(login.username) + ":" + login.password);
  const String request = "POST /update HTTP/1.1\r\nHost: " + address.toString() +
      ":8080\r\nConnection: close\r\nAuthorization: Basic " + authorization +
      "\r\nX-BlueSquid-OTA: " + token + "\r\nContent-Type: multipart/form-data; boundary=" +
      boundary + "\r\nContent-Length: " + String(length) + "\r\n\r\n";
  return send(request) && send(prefix) && send(header, sizeof(header));
}
bool ControllerRelay::Backend::write(uint8_t* data, size_t size) {
  return mbedtls_sha256_update(&sha, data, size) == 0 && send(data, size);
}
bool ControllerRelay::Backend::verify(const uint8_t* expected) {
  uint8_t digest[32]{};
  return mbedtls_sha256_finish(&sha, digest) == 0 && !memcmp(digest, expected, sizeof(digest));
}
bool ControllerRelay::Backend::activate() {
  // Withhold the closing multipart boundary until the browser's complete file
  // and checksum are verified. The Controller independently verifies the image.
  if (!send("\r\n--" + boundary + "--\r\n")) {
    failure = "Controller did not confirm the update. Check its version before retrying.";
    return false;
  }
  client.setTimeout(20000);
  const String status = client.readStringUntil('\n');
  client.stop();
  if (status.startsWith("HTTP/1.1 200 ") || status.startsWith("HTTP/1.0 200 ")) return true;
  failure = "Controller did not confirm the update. Check its version before retrying.";
  return false;
}
#endif
