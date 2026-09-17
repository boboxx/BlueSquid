#include "CanManager.h"

#include <string.h>

#include "AppConfig.h"
#include "BlueSquidCanProtocol.h"
#include "Logging.h"

namespace {
constexpr char kTag[] = "CAN";
}

CanManager::CanManager(OutputController& outputs, BatteryManager& battery,
                       SensorManager& sensors)
    : outputs_(outputs), battery_(battery), sensors_(sensors) {}

bool CanManager::begin() {
  const twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(
      static_cast<gpio_num_t>(AppConfig::Can::kTxPin),
      static_cast<gpio_num_t>(AppConfig::Can::kRxPin), TWAI_MODE_NORMAL);
  const twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&general, &timing, &filter) != ESP_OK ||
      twai_start() != ESP_OK) {
    LOG_ERROR(kTag, "Unable to start TWAI on TX=%d RX=%d",
              AppConfig::Can::kTxPin, AppConfig::Can::kRxPin);
    return false;
  }
  initialized_ = true;
  LOG_INFO(kTag, "BlueSquid CAN started at 250 kbit/s on TX=%d RX=%d",
           AppConfig::Can::kTxPin, AppConfig::Can::kRxPin);
  return true;
}

bool CanManager::transmit(uint32_t id, const uint8_t* data, uint8_t length) {
  if (!initialized_ || length > 8) return false;
  twai_message_t message{};
  message.identifier = id;
  message.data_length_code = length;
  memcpy(message.data, data, length);
  return twai_transmit(&message, pdMS_TO_TICKS(5)) == ESP_OK;
}

void CanManager::update() {
  if (!initialized_) return;
  twai_message_t message{};
  while (twai_receive(&message, 0) == ESP_OK) {
    if (!message.extd && !message.rtr &&
        message.identifier == BlueSquidCan::kCommand) {
      processCommand(message);
    }
  }
}

void CanManager::processCommand(const twai_message_t& message) {
  if (message.data_length_code < 6 ||
      message.data[0] != BlueSquidCan::kProtocolVersion) return;
  const uint8_t sequence = message.data[1];
  const auto command = static_cast<BlueSquidCan::Command>(message.data[2]);
  const uint8_t target = message.data[3];
  const uint16_t value = BlueSquidCan::readU16(message.data, 4);
  BlueSquidCan::Ack result = BlueSquidCan::Ack::Accepted;

  switch (command) {
    case BlueSquidCan::Command::SetRgbw: {
      const uint8_t zone = target >> 4;
      const uint8_t channel = target & 0x0F;
      if (zone > static_cast<uint8_t>(RgbwZone::Output4) || channel > 3 ||
          value > 100 || !outputs_.setRgbwChannel(
              static_cast<RgbwZone>(zone), channel, value))
        result = BlueSquidCan::Ack::InvalidValue;
      break;
    }
    case BlueSquidCan::Command::SetRgbwPreset: {
      const uint8_t zone = target >> 4;
      const uint8_t field = target & 0x0F;
      if (zone > static_cast<uint8_t>(RgbwZone::Output4) || field > 4 ||
          value > 100 ||
          !outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), field,
                                       value))
        result = BlueSquidCan::Ack::InvalidValue;
      break;
    }
    case BlueSquidCan::Command::SetFan:
      if (value > 100 || !outputs_.setFanSpeed(value))
        result = BlueSquidCan::Ack::InvalidValue;
      break;
    case BlueSquidCan::Command::SetUsb:
      if (value > 1) result = BlueSquidCan::Ack::InvalidValue;
      else outputs_.setUsbEnabled(value != 0);
      break;
    case BlueSquidCan::Command::SetPump:
      if (value > 1) result = BlueSquidCan::Ack::InvalidValue;
      else outputs_.setWaterPumpEnabled(value != 0);
      break;
    case BlueSquidCan::Command::SetAccessory3:
      if (value > 1) result = BlueSquidCan::Ack::InvalidValue;
      else outputs_.setAccessory3Enabled(value != 0);
      break;
    case BlueSquidCan::Command::SetAccessory4:
      if (value > 1) result = BlueSquidCan::Ack::InvalidValue;
      else outputs_.setAccessory4Enabled(value != 0);
      break;
    case BlueSquidCan::Command::ApplyScene:
      if (value > static_cast<uint8_t>(LightingScene::Travel))
        result = BlueSquidCan::Ack::InvalidValue;
      else outputs_.applyScene(static_cast<LightingScene>(value));
      break;
    case BlueSquidCan::Command::RequestStatus:
      break;
    case BlueSquidCan::Command::SetAllLights:
      if (value > 1) result = BlueSquidCan::Ack::InvalidValue;
      else outputs_.setAllLightsEnabled(value != 0);
      break;
    case BlueSquidCan::Command::CalibrateLevel:
      if (!sensors_.calibrateLevel()) result = BlueSquidCan::Ack::InvalidValue;
      break;
    case BlueSquidCan::Command::SetBatteryCapacity:
      if (!battery_.setCapacityAh(value / 10.0F))
        result = BlueSquidCan::Ack::InvalidValue;
      break;
    case BlueSquidCan::Command::SetLevelCalibration: {
      const float setting = static_cast<int16_t>(value) / 100.0F;
      if (target > 1 || !sensors_.setLevelCalibration(
              target == 0 ? setting : sensors_.pitchZeroDegrees(),
              target == 1 ? setting : sensors_.rollZeroDegrees())) {
        result = BlueSquidCan::Ack::InvalidValue;
      }
      break;
    }
    default:
      result = BlueSquidCan::Ack::InvalidCommand;
      break;
  }
  lastCommandMs_ = millis();
  acknowledge(sequence, static_cast<uint8_t>(command),
              static_cast<uint8_t>(result));
}

void CanManager::acknowledge(uint8_t sequence, uint8_t command,
                             uint8_t result) {
  const uint8_t data[4] = {BlueSquidCan::kProtocolVersion, sequence, command,
                           result};
  transmit(BlueSquidCan::kCommandAck, data, sizeof(data));
}

void CanManager::publishStatus(const SystemStatus& status) {
  if (!initialized_) return;
  uint8_t data[8]{};
  data[0] = BlueSquidCan::kProtocolVersion;
  data[1] = statusSequence_++;
  data[2] = static_cast<uint8_t>(status.uptimeSeconds);
  data[3] = static_cast<uint8_t>(status.uptimeSeconds >> 8);
  data[4] = static_cast<uint8_t>(status.uptimeSeconds >> 16);
  data[5] = static_cast<uint8_t>(status.uptimeSeconds >> 24);
  data[6] = status.battery.valid ? 1 : 0;
  transmit(BlueSquidCan::kHeartbeat, data, 7);

  memset(data, 0, sizeof(data));
  data[0] = BlueSquidCan::kProtocolVersion;
  data[1] = AppConfig::kFirmwareVersionMajor;
  data[2] = AppConfig::kFirmwareVersionMinor;
  data[3] = AppConfig::kFirmwareVersionPatch;
  transmit(BlueSquidCan::kSystemInfo, data, 4);

  memset(data, 0, sizeof(data));
  BlueSquidCan::writeU16(data, 0, BlueSquidCan::scaled(status.battery.voltage, 1000));
  BlueSquidCan::writeI16(data, 2, BlueSquidCan::scaled(status.battery.current, 100));
  BlueSquidCan::writeU16(data, 4, BlueSquidCan::scaled(status.battery.stateOfCharge, 10));
  data[6] = (status.battery.shuntValid ? 1 : 0) |
            (status.battery.solarValid ? 2 : 0) |
            (status.battery.dcDcValid ? 4 : 0);
  transmit(BlueSquidCan::kBattery, data, 7);

  BlueSquidCan::writeI16(data, 0, BlueSquidCan::scaled(status.battery.power, 1));
  BlueSquidCan::writeU16(data, 2, BlueSquidCan::scaled(status.battery.solarPower, 1));
  BlueSquidCan::writeU16(data, 4, BlueSquidCan::scaled(status.battery.dcDcPower, 1));
  BlueSquidCan::writeU16(data, 6, BlueSquidCan::scaled(status.battery.loadPower, 1));
  transmit(BlueSquidCan::kPower, data, 8);

  BlueSquidCan::writeI16(data, 0, BlueSquidCan::scaled(status.battery.consumedAh, 10));
  BlueSquidCan::writeU16(data, 2, BlueSquidCan::scaled(status.battery.remainingAh, 10));
  BlueSquidCan::writeU16(data, 4, BlueSquidCan::scaled(status.battery.timeToGoMinutes, 1));
  data[6] = status.battery.solarChargerState;
  data[7] = status.battery.dcDcChargerState;
  transmit(BlueSquidCan::kCapacity, data, 8);

  BlueSquidCan::writeI16(data, 0, BlueSquidCan::scaled(status.sensors.cabinTemperatureC, 10));
  BlueSquidCan::writeI16(data, 2, BlueSquidCan::scaled(status.sensors.fridgeTemperatureC, 10));
  BlueSquidCan::writeU16(data, 4, BlueSquidCan::scaled(status.sensors.cabinHumidityPercent, 10));
  data[6] = status.sensors.valid ? 1 : 0;
  transmit(BlueSquidCan::kClimate, data, 7);

  memset(data, 0, sizeof(data));
  BlueSquidCan::writeI16(data, 0,
      BlueSquidCan::scaled(status.sensors.pitchDegrees, 100));
  BlueSquidCan::writeI16(data, 2,
      BlueSquidCan::scaled(status.sensors.rollDegrees, 100));
  data[4] = status.sensors.valid ? 1 : 0;
  transmit(BlueSquidCan::kLevel, data, 5);

  memset(data, 0, sizeof(data));
  data[3] = status.outputs.fanSpeed;
  data[4] = (status.outputs.usbEnabled ? 1 : 0) |
            (status.outputs.waterPumpEnabled ? 2 : 0) |
            (status.outputs.accessory3Enabled ? 4 : 0) |
            (status.outputs.accessory4Enabled ? 8 : 0);
  transmit(BlueSquidCan::kOutputs, data, 8);

  for (uint8_t zone = 0; zone < 4; ++zone) {
    transmit(BlueSquidCan::kRgbwOutputIds[zone], status.outputs.rgbw[zone], 4);
    const uint8_t preset[5] = {status.outputs.rgb[zone][0], status.outputs.rgb[zone][1],
        status.outputs.rgb[zone][2], status.outputs.rgbwBrightness[zone], status.outputs.rgbwOptions[zone]};
    transmit(BlueSquidCan::kRgbwPresetIds[zone], preset, 5);
  }

  uint8_t settings[6]{};
  BlueSquidCan::writeU16(settings, 0,
                         BlueSquidCan::scaled(battery_.capacityAh(), 10));
  BlueSquidCan::writeI16(settings, 2,
                         BlueSquidCan::scaled(sensors_.pitchZeroDegrees(),
                                              100));
  BlueSquidCan::writeI16(settings, 4,
                         BlueSquidCan::scaled(sensors_.rollZeroDegrees(),
                                              100));
  transmit(BlueSquidCan::kSettings, settings, 6);
}

bool CanManager::connected() const {
  return lastCommandMs_ != 0 && millis() - lastCommandMs_ < 10000;
}
