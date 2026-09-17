#include "RgbwBleDriverManager.h"

#include <string.h>
#include <algorithm>

RgbwBleDriverManager::RgbwBleDriverManager(OutputController& outputs)
    : outputs_(outputs) {}

void RgbwBleDriverManager::setAdapter(uint8_t zone,
                                      RgbwBleDriverAdapter* adapter, uint8_t channel) {
  if (zone >= kRgbwZoneCount) return;
  rgbwAdapters_[zone] = adapter;
  outputs_.setRgbwExternal(zone, adapter != nullptr);
  rgbwChannels_[zone] = channel;
  registerAdapter(adapter);
}

void RgbwBleDriverManager::setAccessoryAdapter(
    uint8_t accessory, RgbwBleDriverAdapter* adapter, uint8_t channel) {
  if (accessory >= kAccessoryCount || channel > 4) return;
  accessoryAdapters_[accessory].adapter = adapter;
  accessoryAdapters_[accessory].channel = channel;
  registerAdapter(adapter);
}

void RgbwBleDriverManager::clearAssignments() {
  for (uint8_t zone = 0; zone < kRgbwZoneCount; ++zone)
    outputs_.setRgbwExternal(zone, false);
  memset(rgbwAdapters_, 0, sizeof(rgbwAdapters_));
  memset(accessoryAdapters_, 0, sizeof(accessoryAdapters_));
  memset(adapters_, 0, sizeof(adapters_));
}

void RgbwBleDriverManager::registerAdapter(RgbwBleDriverAdapter* adapter) {
  if (adapter == nullptr) return;
  for (RgbwBleDriverAdapter* existing : adapters_)
    if (existing == adapter) return;
  for (RgbwBleDriverAdapter*& slot : adapters_) {
    if (slot == nullptr) {
      slot = adapter;
      return;
    }
  }
}

void RgbwBleDriverManager::begin() {
  const uint32_t now = millis();
  for (uint8_t index = 0; index < kAdapterCount; ++index) {
    desired_[index] = collectFor(adapters_[index]);
    changedMs_[index] = now;
    // Query on startup before applying any restored local settings.
    pending_[index] = false;
    if (adapters_[index] != nullptr) adapters_[index]->begin();
  }
}

void RgbwBleDriverManager::update() {
  const uint32_t now = millis();
  for (uint8_t index = 0; index < kAdapterCount; ++index) {
    RgbwBleDriverAdapter* adapter = adapters_[index];
    if (adapter == nullptr) continue;
    adapter->update();
    const RgbwBleDriverState current = collectFor(adapter);
    if (!equal(current, desired_[index])) {
      desired_[index] = current;
      if (!pending_[index]) changedMs_[index] = now;
      pending_[index] = true;
    }
    RgbwBleDriverState reported;
    uint32_t revision = 0;
    if (!pending_[index] && adapter->reported(reported, revision) &&
        revision != reportRevision_[index]) {
      reportRevision_[index] = revision;
      if (!equal(current, reported)) applyReport(adapter, reported);
      // Feedback must not become a new outbound command.
      desired_[index] = collectFor(adapter);
    }
    if (!pending_[index] || !adapter->ready() ||
        now - changedMs_[index] < kSettleMs ||
        now - lastAttemptMs_[index] < kRetryMs) {
      continue;
    }
    lastAttemptMs_[index] = now;
    if (adapter->send(desired_[index])) pending_[index] = false;
  }
}

RgbwBleDriverState RgbwBleDriverManager::collectFor(
    RgbwBleDriverAdapter* adapter) const {
  const OutputStatus& output = outputs_.status();
  RgbwBleDriverState state{};
  if (!adapter) return state;
  for (uint8_t zone = 0; zone < kRgbwZoneCount; ++zone) {
    if (rgbwAdapters_[zone] != adapter) continue;
    if (rgbwChannels_[zone] <= 4) {
      state.channels[rgbwChannels_[zone]] = std::max(output.rgbw[zone][0], std::max(output.rgbw[zone][1], std::max(output.rgbw[zone][2], output.rgbw[zone][3])));
      continue;
    }
    memcpy(state.channels, output.rgbw[zone], 4);
    state.channels[3] = (output.rgbwOptions[zone] & 2) ? output.rgbw[zone][3] : 0;
    state.channels[4] = (output.rgbwOptions[zone] & 4) ? output.rgbw[zone][3] : 0;
    memcpy(state.color, output.rgb[zone], 3);
    state.brightness = output.rgbwBrightness[zone];
    state.options = (output.rgbwOptions[zone] & 1) | ((output.rgbwOptions[zone] & 6) ? 2 : 0);
    return state;  // Full-strip assignment owns the physical controller.
  }
  const uint8_t accessoryLevels[4] = {
      static_cast<uint8_t>(output.usbEnabled ? 100 : 0),
      static_cast<uint8_t>(output.waterPumpEnabled ? 100 : 0),
      static_cast<uint8_t>(output.accessory3Enabled ? 100 : 0),
      static_cast<uint8_t>(output.accessory4Enabled ? 100 : 0)};
  for (uint8_t accessory = 0; accessory < kAccessoryCount; ++accessory) {
    if (accessoryAdapters_[accessory].adapter == adapter)
      state.channels[accessoryAdapters_[accessory].channel] =
          accessoryLevels[accessory];
  }
  state.independentChannels = true;
  // Individual assignments still need mode flags so the adapter knows which
  // portions of an RGBW device are intentionally active.
  if (state.channels[0] != 0 || state.channels[1] != 0 ||
      state.channels[2] != 0)
    state.options |= 0x01;
  if (state.channels[3] != 0 || state.channels[4] != 0) state.options |= 0x02;
  return state;
}

bool RgbwBleDriverManager::equal(const RgbwBleDriverState& left,
                                 const RgbwBleDriverState& right) {
  return memcmp(&left, &right, sizeof(left)) == 0;
}

void RgbwBleDriverManager::applyReport(RgbwBleDriverAdapter* adapter,
                                      const RgbwBleDriverState& state) {
  for (uint8_t zone = 0; zone < kRgbwZoneCount; ++zone) {
    if (rgbwAdapters_[zone] == adapter) {
      if (rgbwChannels_[zone] <= 4) {
        const uint8_t level = state.channels[rgbwChannels_[zone]];
        const auto& current = outputs_.status();
        for (uint8_t channel = 0; channel < 4; ++channel) {
          const uint8_t desired = channel == 3 ? level : 0;
          const uint8_t existing = current.rgbw[zone][channel];
          if (existing != desired)
            outputs_.setRgbwChannel(static_cast<RgbwZone>(zone), channel, desired);
        }
        if (level && current.rgbwBrightness[zone] != level)
          outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), 3, level);
        if (current.rgbwOptions[zone] != 2)
          outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), 4, 2);
        continue;
      }
      // Channels first, then the preset, so OutputController's delayed
      // channel-to-preset conversion cannot overwrite the reported preset.
      for (uint8_t channel = 0; channel < 4; ++channel)
        outputs_.setRgbwChannel(static_cast<RgbwZone>(zone), channel,
                                channel == 3 ? std::max(state.channels[3], state.channels[4]) : state.channels[channel]);
      const bool hasColour = state.color[0] || state.color[1] || state.color[2];
      for (uint8_t field = 0; field < 3; ++field) {
        const uint8_t selected = hasColour ? state.color[field] : outputs_.status().rgb[zone][field];
        outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), field, selected);
      }
      outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), 3, state.brightness);
      // A coexistence mode flag is capability, not an active RGB selection.
      const bool anyOutput = state.channels[0] || state.channels[1] || state.channels[2] ||
                             state.channels[3] || state.channels[4];
      const uint8_t colourOption = anyOutput
          ? ((state.channels[0] || state.channels[1] || state.channels[2]) ? 1 : 0)
          : (state.options & 1);
      outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), 4,
          colourOption | ((state.channels[3] || state.channels[4])
              ? (state.channels[3] ? 2 : 0) | (state.channels[4] ? 4 : 0)
              : ((state.options & 2) ? ((outputs_.status().rgbwOptions[zone] & 6) ? (outputs_.status().rgbwOptions[zone] & 6) : 2) : 0)));
      return;
    }
  }
  for (uint8_t index = 0; index < kAccessoryCount; ++index) {
    const auto& assignment = accessoryAdapters_[index];
    if (assignment.adapter != adapter) continue;
    const bool enabled = state.channels[assignment.channel] != 0;
    const bool current[] = {outputs_.status().usbEnabled, outputs_.status().waterPumpEnabled,
        outputs_.status().accessory3Enabled, outputs_.status().accessory4Enabled};
    if (current[index] == enabled) continue;
    switch (index) {
      case 0: outputs_.setUsbEnabled(enabled); break;
      case 1: outputs_.setWaterPumpEnabled(enabled); break;
      case 2: outputs_.setAccessory3Enabled(enabled); break;
      case 3: outputs_.setAccessory4Enabled(enabled); break;
    }
  }
}

void RgbwBleDriverManager::availability(uint8_t& assigned, uint8_t& available) const {
  assigned = available = 0;
  // Assignment bits: RGBW lights 0..3, accessories 4..7.
  for (uint8_t target = 0; target < 8; ++target) {
    const RgbwBleDriverAdapter* adapter = target < 4 ? rgbwAdapters_[target] : accessoryAdapters_[target - 4].adapter;
    if (!adapter) continue;
    assigned |= 1U << target;
    if (adapter->available()) available |= 1U << target;
  }
}
