#pragma once
#if defined(BLUESQUID_TOUCHSCREEN_FIRMWARE) && BLUESQUID_TOUCHSCREEN_FIRMWARE
#include <Arduino.h>
#include <WiFiClient.h>
#include <mbedtls/sha256.h>
#include "OtaPackage.h"
#include "OtaCredentials.h"

// Streams a verified Controller package through the touchscreen without storing
// a second image in RAM, flash or on an SD card.
class ControllerRelay {
 public:
  ControllerRelay();
  void reset();
  void configure(const uint8_t* address, const OtaCredentials::Value& login);
  void feed(uint8_t* data, size_t size);
  void fail(const char* message) { receiver_.fail(message); }
  bool finish() { return receiver_.finish(); }
  const char* error() const;
 private:
  struct Backend {
    IPAddress address;
    OtaCredentials::Value login;
    WiFiClient client;
    mbedtls_sha256_context sha;
    uint8_t header[OtaPackage::kHeaderSize]{};
    String boundary, failure;
    bool begin(size_t size);
    bool write(uint8_t* data, size_t size);
    bool verify(const uint8_t* expected);
    bool activate();
    void abort() { client.stop(); }
    bool send(const uint8_t* data, size_t size);
    bool send(const String& text) { return send(reinterpret_cast<const uint8_t*>(text.c_str()), text.length()); }
  } backend_;
  OtaPackage::Receiver<Backend> receiver_;
  size_t headerBytes_ = 0;
};
#endif
