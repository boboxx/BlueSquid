#pragma once

#include <Arduino.h>
#include <driver/twai.h>

#include "BlueSquidCanProtocol.h"
#include "TouchRemoteStatus.h"

class TouchCanClient {
 public:
  bool begin();
  void update();
  bool send(BlueSquidCan::Command command, uint8_t target, uint16_t value);
  const TouchRemoteStatus& status() const { return status_; }
  bool connected() const;

 private:
  void process(const twai_message_t& message);
  TouchRemoteStatus status_{};
  uint8_t commandSequence_ = 0;
  bool initialized_ = false;
};
