#pragma once

#include <Arduino.h>

#ifndef LOG_LEVEL
#define LOG_LEVEL 3
#endif

#ifndef BLUESQUID_LOG_INFO_ENABLED
#define BLUESQUID_LOG_INFO_ENABLED 1
#endif

#define LOG_ERROR(tag, format, ...) \
  do { if (LOG_LEVEL >= 1) Serial.printf("[E][%s] " format "\n", tag, ##__VA_ARGS__); } while (0)
#define LOG_WARN(tag, format, ...) \
  do { if (LOG_LEVEL >= 2) Serial.printf("[W][%s] " format "\n", tag, ##__VA_ARGS__); } while (0)
#if BLUESQUID_LOG_INFO_ENABLED
#define LOG_INFO(tag, format, ...) \
  do { if (LOG_LEVEL >= 3) Serial.printf("[I][%s] " format "\n", tag, ##__VA_ARGS__); } while (0)
#else
#define LOG_INFO(tag, format, ...) \
  do { } while (0)
#endif
#define LOG_DEBUG(tag, format, ...) \
  do { if (LOG_LEVEL >= 4) Serial.printf("[D][%s] " format "\n", tag, ##__VA_ARGS__); } while (0)
