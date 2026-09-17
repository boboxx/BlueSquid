#pragma once

#include <Arduino.h>
#include <string.h>

// Temporary point-to-point RS-485 envelope for BlueSquid's existing
// application messages. The payload and IDs intentionally match the CAN
// protocol so the UI and rear-controller behavior stay transport independent.
namespace BlueSquidSerial {

constexpr uint8_t kStart0 = 0xA5;
constexpr uint8_t kStart1 = 0x5A;
constexpr uint8_t kMaxPayload = 8;
constexpr size_t kMaxFrameSize = 2 + 2 + 1 + kMaxPayload + 2;

struct Frame {
  uint16_t id = 0;
  uint8_t length = 0;
  uint8_t data[kMaxPayload]{};
};

inline uint16_t updateCrc(uint16_t crc, uint8_t value) {
  crc ^= static_cast<uint16_t>(value) << 8;
  for (uint8_t bit = 0; bit < 8; ++bit)
    crc = (crc & 0x8000U) ? static_cast<uint16_t>((crc << 1) ^ 0x1021U)
                          : static_cast<uint16_t>(crc << 1);
  return crc;
}

inline size_t encode(uint16_t id, const uint8_t* data, uint8_t length,
                     uint8_t* output) {
  if (length > kMaxPayload || output == nullptr) return 0;
  size_t cursor = 0;
  output[cursor++] = kStart0;
  output[cursor++] = kStart1;
  output[cursor++] = static_cast<uint8_t>(id);
  output[cursor++] = static_cast<uint8_t>(id >> 8);
  output[cursor++] = length;
  uint16_t crc = 0xFFFFU;
  crc = updateCrc(crc, output[2]);
  crc = updateCrc(crc, output[3]);
  crc = updateCrc(crc, length);
  for (uint8_t index = 0; index < length; ++index) {
    output[cursor++] = data[index];
    crc = updateCrc(crc, data[index]);
  }
  output[cursor++] = static_cast<uint8_t>(crc);
  output[cursor++] = static_cast<uint8_t>(crc >> 8);
  return cursor;
}

class Parser {
 public:
  bool push(uint8_t value, Frame& result) {
    switch (state_) {
      case State::Start0:
        if (value == kStart0) state_ = State::Start1;
        break;
      case State::Start1:
        if (value == kStart1) state_ = State::IdLow;
        else state_ = value == kStart0 ? State::Start1 : State::Start0;
        break;
      case State::IdLow:
        frame_.id = value;
        crc_ = updateCrc(0xFFFFU, value);
        state_ = State::IdHigh;
        break;
      case State::IdHigh:
        frame_.id |= static_cast<uint16_t>(value) << 8;
        crc_ = updateCrc(crc_, value);
        state_ = State::Length;
        break;
      case State::Length:
        if (value > kMaxPayload) {
          reset();
          break;
        }
        frame_.length = value;
        payloadIndex_ = 0;
        crc_ = updateCrc(crc_, value);
        state_ = value == 0 ? State::CrcLow : State::Payload;
        break;
      case State::Payload:
        frame_.data[payloadIndex_++] = value;
        crc_ = updateCrc(crc_, value);
        if (payloadIndex_ == frame_.length) state_ = State::CrcLow;
        break;
      case State::CrcLow:
        receivedCrc_ = value;
        state_ = State::CrcHigh;
        break;
      case State::CrcHigh: {
        receivedCrc_ |= static_cast<uint16_t>(value) << 8;
        const bool valid = receivedCrc_ == crc_;
        if (valid) result = frame_;
        reset();
        return valid;
      }
    }
    return false;
  }

 private:
  enum class State : uint8_t {
    Start0, Start1, IdLow, IdHigh, Length, Payload, CrcLow, CrcHigh
  };

  void reset() {
    state_ = State::Start0;
    payloadIndex_ = 0;
    receivedCrc_ = 0;
    crc_ = 0xFFFFU;
  }

  State state_ = State::Start0;
  Frame frame_{};
  uint8_t payloadIndex_ = 0;
  uint16_t receivedCrc_ = 0;
  uint16_t crc_ = 0xFFFFU;
};

}  // namespace BlueSquidSerial
