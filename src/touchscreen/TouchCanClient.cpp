#include "TouchCanClient.h"

#include <string.h>

bool TouchCanClient::begin() {
  const twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      GPIO_NUM_20, GPIO_NUM_19, TWAI_MODE_NORMAL);
  const twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  initialized_ = twai_driver_install(&general, &timing, &filter) == ESP_OK &&
                 twai_start() == ESP_OK;
  return initialized_;
}

void TouchCanClient::update() {
  if (!initialized_) return;
  twai_message_t message{};
  while (twai_receive(&message, 0) == ESP_OK) process(message);
}

void TouchCanClient::process(const twai_message_t& message) {
  if (message.extd || message.rtr) return;
  const uint8_t* data = message.data;
  switch (message.identifier) {
    case BlueSquidCan::kHeartbeat:
      if (message.data_length_code >= 7 &&
          data[0] == BlueSquidCan::kProtocolVersion) {
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
      if (message.data_length_code >= 4 &&
          data[0] == BlueSquidCan::kProtocolVersion) {
        status_.rearFirmwareMajor = data[1];
        status_.rearFirmwareMinor = data[2];
        status_.rearFirmwarePatch = data[3];
        status_.systemInfoValid = true;
      }
      break;
    case BlueSquidCan::kBattery:
      if (message.data_length_code >= 7) {
        status_.voltage = BlueSquidCan::readU16(data, 0) / 1000.0F;
        status_.current = BlueSquidCan::readI16(data, 2) / 100.0F;
        status_.soc = BlueSquidCan::readU16(data, 4) / 10.0F;
      }
      break;
    case BlueSquidCan::kPower:
      if (message.data_length_code == 8) {
        status_.batteryPower = BlueSquidCan::readI16(data, 0);
        status_.solarPower = BlueSquidCan::readU16(data, 2);
        status_.dcDcPower = BlueSquidCan::readU16(data, 4);
        status_.loadPower = BlueSquidCan::readU16(data, 6);
      }
      break;
    case BlueSquidCan::kCapacity:
      if (message.data_length_code == 8) {
        status_.remainingAh = BlueSquidCan::readU16(data, 2) / 10.0F;
        status_.timeToGoMinutes = BlueSquidCan::readU16(data, 4);
        status_.solarState = data[6];
        status_.dcDcState = data[7];
      }
      break;
    case BlueSquidCan::kClimate:
      if (message.data_length_code >= 7) {
        status_.cabinTemperatureC = BlueSquidCan::readI16(data, 0) / 10.0F;
        status_.fridgeTemperatureC = BlueSquidCan::readI16(data, 2) / 10.0F;
        status_.humidity = BlueSquidCan::readU16(data, 4) / 10.0F;
      }
      break;
    case BlueSquidCan::kLevel:
      if (message.data_length_code >= 5) {
        status_.pitchDegrees = BlueSquidCan::readI16(data, 0) / 100.0F;
        status_.rollDegrees = BlueSquidCan::readI16(data, 2) / 100.0F;
        status_.levelValid = data[4] != 0;
      }
      break;
    case BlueSquidCan::kOutputs:
      if (message.data_length_code >= 5) {
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
      if (message.data_length_code >= 4) {
        for (uint8_t zone = 0; zone < 4; ++zone)
          if (message.identifier == BlueSquidCan::kRgbwOutputIds[zone]) memcpy(status_.rgbw[zone], data, 4);
      }
      break;
    case BlueSquidCan::kFrontRgbwPreset:
    case BlueSquidCan::kBedRgbwPreset:
    case BlueSquidCan::kRgbw3Preset:
    case BlueSquidCan::kRgbw4Preset:
      if (message.data_length_code >= 5) {
        for (uint8_t zone = 0; zone < 4; ++zone) {
          if (message.identifier != BlueSquidCan::kRgbwPresetIds[zone]) continue;
          memcpy(status_.rgb[zone], data, 3);
          status_.rgbwBrightness[zone] = data[3];
          status_.rgbwOptions[zone] = data[4];
          status_.rgbwPresetValid[zone] = true;
        }
      }
      break;
    case BlueSquidCan::kSettings:
      if (message.data_length_code >= 6) {
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

bool TouchCanClient::send(BlueSquidCan::Command command, uint8_t target,
                          uint16_t value) {
  if (!initialized_) return false;
  twai_message_t message{};
  message.identifier = BlueSquidCan::kCommand;
  message.data_length_code = 6;
  message.data[0] = BlueSquidCan::kProtocolVersion;
  message.data[1] = commandSequence_++;
  message.data[2] = static_cast<uint8_t>(command);
  message.data[3] = target;
  BlueSquidCan::writeU16(message.data, 4, value);
  return twai_transmit(&message, pdMS_TO_TICKS(20)) == ESP_OK;
}

bool TouchCanClient::connected() const {
  return status_.lastHeartbeatMs != 0 &&
         millis() - status_.lastHeartbeatMs <= BlueSquidCan::kStatusTimeoutMs;
}
