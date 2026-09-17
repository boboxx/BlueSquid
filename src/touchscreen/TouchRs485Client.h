#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include "BlueSquidCanProtocol.h"
#include "BlueSquidSerialProtocol.h"
#include "TouchRemoteStatus.h"

class TouchRs485Client {
 public:
  bool begin();
  void update();
  bool send(BlueSquidCan::Command command, uint8_t target, uint16_t value);
  const TouchRemoteStatus& status() const { return status_; }
  bool connected() const;
  uint32_t receivedByteCount() const {
    return receivedByteCount15_ + receivedByteCount16_;
  }
  uint32_t receivedByteCount15() const { return receivedByteCount15_; }
  uint32_t receivedByteCount16() const { return receivedByteCount16_; }
  uint32_t validFrameCount() const {
    return validFrameCount15_ + validFrameCount16_;
  }
  int detectedRxPin() const { return detectedRxPin_; }

 private:
  void process(const BlueSquidSerial::Frame& frame);
  void pollCandidate(HardwareSerial& serial, BlueSquidSerial::Parser& parser,
                     int rxPin, uint32_t& byteCount,
                     uint32_t& frameCount);
  void selectPins(int rxPin);
  HardwareSerial uart15_{1};
  HardwareSerial uart16_{2};
  HardwareSerial* activeSerial_ = nullptr;
  BlueSquidSerial::Parser parser15_{};
  BlueSquidSerial::Parser parser16_{};
  TouchRemoteStatus status_{};
  uint8_t commandSequence_ = 0;
  uint32_t receivedByteCount15_ = 0;
  uint32_t receivedByteCount16_ = 0;
  uint32_t validFrameCount15_ = 0;
  uint32_t validFrameCount16_ = 0;
  uint32_t lastProbeMs_ = 0;
  int detectedRxPin_ = -1;
  bool initialized_ = false;
};
