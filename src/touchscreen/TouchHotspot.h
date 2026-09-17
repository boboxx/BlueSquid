#pragma once
#include <Arduino.h>

namespace TouchHotspot {
struct Status {
  char ssid[33]{};
  char ip[20]{};
  bool active = false;
  uint8_t clients = 0;
};
using StatusProvider = String (*)();
using CommandHandler = bool (*)(const String&, uint8_t, uint32_t);
void setRemoteHandlers(StatusProvider status, CommandHandler command);
bool begin();
void update();
bool configure(const String& ssid, const String& password);
Status status();
}
