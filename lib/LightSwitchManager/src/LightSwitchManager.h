#pragma once

#include <Arduino.h>

#include "OutputController.h"

class LightSwitchManager {
 public:
  explicit LightSwitchManager(OutputController& outputController);

  void begin();
  bool update();
  // A 5-second hold opens the Bluetooth pairing window (consumed once).
  bool consumePairingRequest();

 private:
  bool readPressed() const;
  void apply(bool enabled);

  OutputController& outputController_;
  bool rawPressed_ = false;
  bool stablePressed_ = false;
  bool lightsEnabled_ = false;
  bool initialized_ = false;
  bool holdHandled_ = false;
  bool pairingRequested_ = false;
  uint32_t rawStateChangedMs_ = 0;
  uint32_t pressedMs_ = 0;
};
