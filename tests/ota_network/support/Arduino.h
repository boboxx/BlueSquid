#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string>
class String : public std::string {
 public:
  using std::string::string;
  bool isEmpty() const { return empty(); }
  int indexOf(char value) const { const auto i = find(value); return i == npos ? -1 : static_cast<int>(i); }
};
uint32_t millis();
