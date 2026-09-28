#pragma once
#include <stdint.h>
#include "Sp630eChannels.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

constexpr uint8_t kSp630eAssignmentCount = 8;
struct Sp630eAssignment {
  char address[18]{};
  uint8_t channel = 255;
};

// Complete configuration: group mask|target,channel,address;... (all 8 rows).
inline bool parseSp630eConfiguration(const char* text,
    Sp630eAssignment (&rows)[8], uint8_t& group) {
  unsigned mask = 0; int used = 0;
  if (sscanf(text, "%u|%n", &mask, &used) != 1 || used == 0 || mask > 15) return false;
  text += used;
  Sp630eAssignment parsed[8]{};
  unsigned devices = 0;
  for (unsigned i = 0; i < 8; ++i) {
    unsigned target = 0, channel = 0; char address[18]{}; used = 0;
    if (sscanf(text, "%u,%u,%17[^;];%n", &target, &channel, address, &used) != 3 ||
        !used || target != i || (channel > 4 && !Sp630eChannels::colourType(channel)) ||
        (i >= 4 && Sp630eChannels::colourType(channel) && strcmp(address, "none") != 0)) return false;
    text += used;
    parsed[i].channel = channel;
    if (strcmp(address, "none") == 0) continue;
    if (strlen(address) != 17) return false;
    for (unsigned j = 0; j < 17; ++j)
      if (j % 3 == 2 ? address[j] != ':' : !isxdigit(static_cast<unsigned char>(address[j]))) return false;
    strcpy(parsed[i].address, address);
    bool known = false;
    for (unsigned j = 0; j < i; ++j) {
      if (strcasecmp(parsed[j].address, address)) continue;
      known = true;
      if (Sp630eChannels::colourType(channel) || Sp630eChannels::colourType(parsed[j].channel) || channel == parsed[j].channel) return false;
    }
    if (!known && ++devices > 5) return false;
  }
  if (*text) return false;
  memcpy(rows, parsed, sizeof parsed); group = mask;
  return true;
}
