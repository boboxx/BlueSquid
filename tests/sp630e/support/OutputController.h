#pragma once
#include "SystemTypes.h"
class OutputController {
 public:
  OutputStatus value{};
  unsigned changes = 0;
  bool externalRgbw[4]{};
  void setRgbwExternal(uint8_t zone, bool external) { externalRgbw[zone] = external; }
  const OutputStatus& status() const { return value; }
  void setRgbwChannel(RgbwZone zone, uint8_t channel, uint8_t level) {
    value.rgbw[static_cast<unsigned>(zone)][channel] = level; ++changes;
  }
  void setRgbwPresetField(RgbwZone zone, uint8_t field, uint8_t level) {
    const unsigned z = static_cast<unsigned>(zone);
    if (field < 3) value.rgb[z][field] = level;
    else if (field == 3) value.rgbwBrightness[z] = level;
    else value.rgbwOptions[z] = level;
    ++changes;
  }
  void setUsbEnabled(bool on) { value.usbEnabled = on; ++changes; }
  void setWaterPumpEnabled(bool on) { value.waterPumpEnabled = on; ++changes; }
  void setAccessory3Enabled(bool on) { value.accessory3Enabled = on; ++changes; }
  void setAccessory4Enabled(bool on) { value.accessory4Enabled = on; ++changes; }
};
