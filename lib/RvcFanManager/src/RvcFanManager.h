#pragma once
#include <driver/twai.h>
#include "OutputController.h"
#include "SettingsManager.h"
#include "RvcFanProtocol.h"

class RvcFanManager {
 public:
  RvcFanManager(OutputController& outputs, SettingsManager& settings)
      : outputs_(outputs), settings_(settings) {}
  bool begin();
  void update();
  bool setSpeed(uint8_t speed);
  bool setDirection(bool intake);
 private:
  bool send(uint32_t dgn, const uint8_t (&data)[8], uint8_t destination = 255, bool probe = false);
  void probe();
  void conflict();
  void publish();
  void diagnostic();
  OutputController& outputs_;
  SettingsManager& settings_;
  RvcFan::Config config_{};
  RvcFan::Status status_{};
  bool initialized_ = false, seen_ = false, pending_ = false, sent_ = false;
  uint8_t source_ = 159, phase_ = 0, desired_ = 0, error_ = 0;
  bool speedPending_ = false, directionPending_ = false, desiredIntake_ = false;
  uint8_t name_[8]{};
  uint32_t configRevision_ = 0;
  uint32_t phaseMs_ = 0, lastSeenMs_ = 0, queuedMs_ = 0, sentMs_ = 0, pollMs_ = 0, diagnosticMs_ = 0;
};
