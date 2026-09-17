#include "TouchRs485Client.h"

#include <string.h>

#ifndef BLUESQUID_TOUCH_RS485_RX_PIN
#define BLUESQUID_TOUCH_RS485_RX_PIN 16
#endif
#ifndef BLUESQUID_TOUCH_RS485_TX_PIN
#define BLUESQUID_TOUCH_RS485_TX_PIN 15
#endif

bool TouchRs485Client::begin() {
  // Waveshare's published table/schematic and Arduino example disagree about
  // GPIO15/16 direction. Listen on both without driving either pin, then bind
  // TX to the opposite pin after a valid BlueSquid status frame is detected.
  uart15_.begin(115200, SERIAL_8N1, 15, -1);
  uart16_.begin(115200, SERIAL_8N1, 16, -1);
  initialized_ = true;
  return true;
}

void TouchRs485Client::update() {
  if (!initialized_) return;
  if (detectedRxPin_ < 0) {
    pollCandidate(uart15_, parser15_, 15, receivedByteCount15_,
                  validFrameCount15_);
    if (detectedRxPin_ < 0)
      pollCandidate(uart16_, parser16_, 16, receivedByteCount16_,
                    validFrameCount16_);
  } else if (detectedRxPin_ == 15) {
    pollCandidate(uart15_, parser15_, 15, receivedByteCount15_,
                  validFrameCount15_);
  } else {
    pollCandidate(uart16_, parser16_, 16, receivedByteCount16_,
                  validFrameCount16_);
  }
  // Keep exercising the opposite direction while disconnected. The interval
  // deliberately does not align with the rear controller's 1-second status
  // publication, reducing the chance of repeated half-duplex collisions.
  const uint32_t now = millis();
  if (!connected() && now - lastProbeMs_ >= 1700) {
    lastProbeMs_ = now;
    send(BlueSquidCan::Command::RequestStatus, 0, 0);
  }
}

void TouchRs485Client::pollCandidate(HardwareSerial& serial,
                                     BlueSquidSerial::Parser& parser,
                                     int rxPin, uint32_t& byteCount,
                                     uint32_t& frameCount) {
  BlueSquidSerial::Frame frame{};
  while (serial.available() > 0) {
    ++byteCount;
    if (parser.push(static_cast<uint8_t>(serial.read()), frame)) {
      ++frameCount;
      process(frame);
      if (detectedRxPin_ < 0 && frame.id != BlueSquidCan::kCommand) {
        selectPins(rxPin);
        return;
      }
    }
  }
}

void TouchRs485Client::selectPins(int rxPin) {
  detectedRxPin_ = rxPin;
  uart15_.end();
  uart16_.end();
  delay(2);
  if (rxPin == 15) {
    uart15_.begin(115200, SERIAL_8N1, 15, 16);
    activeSerial_ = &uart15_;
  } else {
    uart16_.begin(115200, SERIAL_8N1, 16, 15);
    activeSerial_ = &uart16_;
  }
}

void TouchRs485Client::process(const BlueSquidSerial::Frame& frame) {
  const uint8_t* data = frame.data;
  switch (frame.id) {
    case BlueSquidCan::kHeartbeat:
      if (frame.length >= 7 && data[0] == BlueSquidCan::kProtocolVersion) {
        status_.rearUptimeSeconds =
            static_cast<uint32_t>(data[2]) |
            (static_cast<uint32_t>(data[3]) << 8) |
            (static_cast<uint32_t>(data[4]) << 16) |
            (static_cast<uint32_t>(data[5]) << 24);
        status_.energyValid = data[6] != 0;
        status_.lastHeartbeatMs = millis();
      }
      break;
    case BlueSquidCan::kSystemInfo:
      if (frame.length >= 4 && data[0] == BlueSquidCan::kProtocolVersion) {
        status_.rearFirmwareMajor = data[1];
        status_.rearFirmwareMinor = data[2];
        status_.rearFirmwarePatch = data[3];
        status_.systemInfoValid = true;
      }
      break;
    case BlueSquidCan::kBattery:
      if (frame.length >= 7) {
        status_.voltage = BlueSquidCan::readU16(data, 0) / 1000.0F;
        status_.current = BlueSquidCan::readI16(data, 2) / 100.0F;
        status_.soc = BlueSquidCan::readU16(data, 4) / 10.0F;
      }
      break;
    case BlueSquidCan::kPower:
      if (frame.length == 8) {
        status_.batteryPower = BlueSquidCan::readI16(data, 0);
        status_.solarPower = BlueSquidCan::readU16(data, 2);
        status_.dcDcPower = BlueSquidCan::readU16(data, 4);
        status_.loadPower = BlueSquidCan::readU16(data, 6);
      }
      break;
    case BlueSquidCan::kCapacity:
      if (frame.length == 8) {
        status_.remainingAh = BlueSquidCan::readU16(data, 2) / 10.0F;
        status_.timeToGoMinutes = BlueSquidCan::readU16(data, 4);
        status_.solarState = data[6];
        status_.dcDcState = data[7];
      }
      break;
    case BlueSquidCan::kClimate:
      if (frame.length >= 7) {
        status_.cabinTemperatureC = BlueSquidCan::readI16(data, 0) / 10.0F;
        status_.fridgeTemperatureC = BlueSquidCan::readI16(data, 2) / 10.0F;
        status_.humidity = BlueSquidCan::readU16(data, 4) / 10.0F;
      }
      break;
    case BlueSquidCan::kLevel:
      if (frame.length >= 5) {
        status_.pitchDegrees = BlueSquidCan::readI16(data, 0) / 100.0F;
        status_.rollDegrees = BlueSquidCan::readI16(data, 2) / 100.0F;
        status_.levelValid = data[4] != 0;
      }
      break;
    case BlueSquidCan::kOutputs:
      if (frame.length >= 5) {
        status_.fan = data[3];
        status_.usb = (data[4] & 1) != 0;
        status_.pump = (data[4] & 2) != 0;
        status_.accessory3 = (data[4] & 4) != 0;
        status_.accessory4 = (data[4] & 8) != 0;
      }
      break;
    case BlueSquidCan::kFrontRgbw:
    case BlueSquidCan::kBedRgbw:
    case BlueSquidCan::kRgbw3:
    case BlueSquidCan::kRgbw4:
      if (frame.length >= 4) {
        for (uint8_t zone = 0; zone < 4; ++zone)
          if (frame.id == BlueSquidCan::kRgbwOutputIds[zone]) memcpy(status_.rgbw[zone], data, 4);
      }
      break;
    case BlueSquidCan::kFrontRgbwPreset:
    case BlueSquidCan::kBedRgbwPreset:
    case BlueSquidCan::kRgbw3Preset:
    case BlueSquidCan::kRgbw4Preset:
      if (frame.length >= 5) {
        for (uint8_t zone = 0; zone < 4; ++zone) {
          if (frame.id != BlueSquidCan::kRgbwPresetIds[zone]) continue;
          memcpy(status_.rgb[zone], data, 3);
          status_.rgbwBrightness[zone] = data[3];
          status_.rgbwOptions[zone] = data[4];
          status_.rgbwPresetValid[zone] = true;
        }
      }
      break;
    case BlueSquidCan::kSettings:
      if (frame.length >= 6) {
        status_.batteryCapacityAh = BlueSquidCan::readU16(data, 0) / 10.0F;
        status_.pitchZeroDegrees = BlueSquidCan::readI16(data, 2) / 100.0F;
        status_.rollZeroDegrees = BlueSquidCan::readI16(data, 4) / 100.0F;
        status_.settingsValid = true;
      }
      break;
    default:
      break;
  }
}

bool TouchRs485Client::send(BlueSquidCan::Command command, uint8_t target,
                            uint16_t value) {
  if (!initialized_ || activeSerial_ == nullptr) return false;
  uint8_t data[6]{};
  data[0] = BlueSquidCan::kProtocolVersion;
  data[1] = commandSequence_++;
  data[2] = static_cast<uint8_t>(command);
  data[3] = target;
  BlueSquidCan::writeU16(data, 4, value);
  uint8_t encoded[BlueSquidSerial::kMaxFrameSize]{};
  const size_t length = BlueSquidSerial::encode(
      BlueSquidCan::kCommand, data, sizeof(data), encoded);
  if (length == 0) return false;
  const bool written = activeSerial_->write(encoded, length) == length;
  activeSerial_->flush();
  return written;
}

bool TouchRs485Client::connected() const {
  return status_.lastHeartbeatMs != 0 &&
         millis() - status_.lastHeartbeatMs <= BlueSquidCan::kStatusTimeoutMs;
}
