#pragma once
#include <stdint.h>
namespace Sp630eChannels {
// Preserve existing persisted IDs: white (3) is WW; CW gets the new ID 4.
constexpr uint8_t count = 5;
// A full-strip light records which outputs are wired. The original IDs keep
// their meaning: 255 is RGB + WW + CW and 254 is RGB only. Other
// combinations are stored as 0xF0 | capabilities.
constexpr uint8_t fullStrip = 255;
constexpr uint8_t rgbOnly = 254;
constexpr uint8_t kColour = 1;
constexpr uint8_t kWarmWhite = 2;
constexpr uint8_t kCoolWhite = 4;
constexpr uint8_t kAllCapabilities = kColour | kWarmWhite | kCoolWhite;
// Assignment dropdown: "Full RGB" then the five single channels.
constexpr uint8_t lightChoiceCount = count + 1;
inline bool colourType(unsigned channel) {
  return channel == fullStrip || channel == rgbOnly ||
         (channel > 0xF0 && channel < 0xF0 + kAllCapabilities + 1);
}
// Capability bits of a full-strip channel; 0 for a single-channel light.
inline uint8_t capabilities(unsigned channel) {
  if (channel == fullStrip) return kAllCapabilities;
  if (channel == rgbOnly) return kColour;
  return colourType(channel) ? channel & kAllCapabilities : 0;
}
inline uint8_t fullStripChannel(uint8_t capabilities) {
  capabilities &= kAllCapabilities;
  if (capabilities == kAllCapabilities || capabilities == 0) return fullStrip;
  if (capabilities == kColour) return rgbOnly;
  return 0xF0 | capabilities;
}
constexpr uint8_t ids[] = {0, 1, 2, 4, 3};
constexpr const char* labels[] = {"R", "G", "B", "CW", "WW"};
inline uint8_t position(uint8_t id) {
  for (uint8_t i = 0; i < count; ++i) if (ids[i] == id) return i;
  return 0;
}
inline uint8_t lightPosition(uint8_t channel) {
  return colourType(channel) ? 0 : position(channel) + 1;
}
// Choosing "Full RGB" keeps the light's enabled outputs; a light that was
// not a full strip starts as RGB only.
inline uint8_t lightChannel(uint8_t choice, uint8_t current = rgbOnly) {
  return choice == 0 ? (colourType(current) ? current : rgbOnly) : ids[choice - 1];
}
}
