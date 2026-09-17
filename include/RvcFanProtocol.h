#pragma once
#include <stdint.h>
#include <stddef.h>

// RVIA RV-C, February 2026, sections 3.3 and 6.46.
namespace RvcFan {
constexpr uint32_t kStatus = 0x1FEA7;
constexpr uint32_t kCommand = 0x1FEA6;
constexpr uint32_t kRequest = 0xEA00;
constexpr uint32_t kClaim = 0xEE00;
constexpr uint32_t kTimeoutMs = 15000;
struct Config {
  uint8_t enabled = 0;
  uint8_t instance = 1;
  uint8_t source = 159; // Control panel: 144–159; 144–150 have assigned static roles.
};
inline bool valid(const Config& c) {
  return c.enabled <= 1 && c.instance >= 1 && c.instance <= 250 &&
         c.source >= 151 && c.source <= 159;
}
inline uint32_t id(uint32_t dgn, uint8_t source, uint8_t destination = 255) {
  if (((dgn >> 8) & 255) < 240) dgn = (dgn & 0x1FF00) | destination;
  return (6UL << 26) | (dgn << 8) | source;
}
inline uint32_t dgn(uint32_t id) {
  const uint32_t value = (id >> 8) & 0x1FFFF;
  return ((value >> 8) & 255) < 240 ? value & 0x1FF00 : value;
}
inline void command(uint8_t instance, uint8_t percent, uint8_t (&data)[8]) {
  for (auto& byte : data) byte = 255; // Unchanged fields, including rain sensor/lid/light.
  data[0] = instance;
  data[1] = percent ? 0xD5 : 0xFC; // On + force fan + manual speed; off changes only system state.
  data[2] = percent * 2; // RV-C percent uses 0.5% units.
}
inline void directionCommand(uint8_t instance, bool intake, uint8_t (&data)[8]) {
  for (auto& byte : data) byte = 255;
  data[0] = instance;
  data[3] = intake ? 0xFD : 0xFC;
}
struct Status { bool on = false; uint8_t speed = 0; uint8_t direction = 3; };
inline bool decode(uint32_t identifier, bool extended, bool remote,
                   const uint8_t* data, size_t length, uint8_t instance, Status& out) {
  if (!extended || remote || length != 8 || dgn(identifier) != kStatus ||
      (identifier & 255) >= 254 || data[0] != instance ||
      (data[1] & 3) > 1 || data[2] > 200) return false;
  out.on = (data[1] & 3) == 1;
  out.speed = (data[2] + 1) / 2;
  out.direction = data[3] & 3;
  return true;
}
inline void request(uint32_t requested, uint8_t (&data)[8]) {
  for (auto& byte : data) byte = 255;
  data[0] = requested; data[1] = requested >> 8; data[2] = requested >> 16;
}
}
