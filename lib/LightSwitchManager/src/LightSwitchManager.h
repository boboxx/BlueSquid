#pragma once

#include <Arduino.h>

#include "OutputController.h"

class LightSwitchManager {
 public:
  explicit LightSwitchManager(OutputController& outputController);

  void begin();
 bool update();

 private:
  bool readPressed() const;
  void apply(bool enabled);

  OutputController& outputController_;
  bool rawPressed_ = false;
  bool stablePressed_ = false;
  bool lightsEnabled_ = false;
  bool initialized_ = false;
  uint32_t rawStateChangedMs_ = 0;
};
