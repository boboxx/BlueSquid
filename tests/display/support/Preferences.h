#pragma once
class Preferences {
 public:
  bool begin(const char*, bool) { return true; }
  bool isKey(const char*) { return false; }
  unsigned getUInt(const char*, unsigned value = 0) { return value; }
  void putUInt(const char*, unsigned) {}
};
