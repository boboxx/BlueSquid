#pragma once
#include <Arduino.h>
constexpr int WIFI_STA = 1, WL_CONNECTED = 3;
struct IPAddress {
  uint8_t bytes[4]{192, 168, 4, 2};
  uint8_t operator[](unsigned i) const { return bytes[i]; }
  String toString() const { return "192.168.4.2"; }
};
struct TestWiFi {
  String ssid, password;
  bool connected = false;
  unsigned starts = 0, disconnects = 0;
  void persistent(bool) {}
  void mode(int) {}
  void setAutoReconnect(bool) {}
  void begin(const char* name, const char* key) { ssid = name; password = key; connected = false; ++starts; }
  void disconnect(bool) { connected = false; ++disconnects; }
  int status() const { return connected ? WL_CONNECTED : 0; }
  String SSID() const { return ssid; }
  IPAddress localIP() const { return {}; }
  IPAddress gatewayIP() const { return {}; }
};
inline TestWiFi WiFi;
