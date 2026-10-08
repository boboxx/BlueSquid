#pragma once
#include "NimBLEDevice.h"
struct Preferences {
 bool begin(const char*, bool) { return true; }
 bool getBool(const char*, bool) { return Fixture::marker; }
 size_t putBool(const char*, bool value) {
  if (!Fixture::writeOk) return 0;
  Fixture::marker = value; return 1;
 }
 void end() {}
};
