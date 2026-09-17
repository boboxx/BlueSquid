#pragma once
#include "RvcFanProtocol.h"
class SettingsManager {
 public:
  RvcFan::Config config{};
  uint32_t revision=0;
  RvcFan::Config loadRvcFanConfiguration() { return config; }
  uint32_t rvcFanRevision() const { return revision; }
};
