#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <algorithm>
using std::max;
using std::min;
#define constrain(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
constexpr int HIGH=1, LOW=0, OUTPUT=1, INPUT_PULLUP=2, INPUT_PULLDOWN=3;
uint32_t millis();
int digitalRead(int pin);
void pinMode(int pin, int mode);
inline void digitalWrite(int, int) {}
inline void delay(unsigned) {}
inline void ledcSetup(unsigned, unsigned, unsigned) {}
inline void ledcAttachPin(int, unsigned) {}
inline void ledcWrite(unsigned, unsigned) {}
struct TestSerial { template<typename... Args> void printf(const char*, Args...) {} };
inline TestSerial Serial;
