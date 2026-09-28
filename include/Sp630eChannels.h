#pragma once
#include <stdint.h>
namespace Sp630eChannels {
// Preserve existing persisted IDs: white (3) is WW; CW gets the new ID 4.
constexpr uint8_t count = 5;
constexpr uint8_t fullStrip = 255;
constexpr uint8_t rgbOnly = 254;
constexpr uint8_t lightChoiceCount = count + 2;
inline bool colourType(unsigned channel) { return channel == fullStrip || channel == rgbOnly; }
constexpr uint8_t ids[] = {0, 1, 2, 4, 3};
constexpr const char* labels[] = {"R", "G", "B", "CW", "WW"};
inline uint8_t position(uint8_t id) {
  for (uint8_t i = 0; i < count; ++i) if (ids[i] == id) return i;
  return 0;
}
inline uint8_t lightPosition(uint8_t channel) {
  return channel == fullStrip ? 0 : channel == rgbOnly ? 1 : position(channel) + 2;
}
inline uint8_t lightChannel(uint8_t choice) {
  return choice == 0 ? fullStrip : choice == 1 ? rgbOnly : ids[choice - 2];
}
}
