#include "RgbwBleDriverManager.h"
#include "Sp630eChannels.h"

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

bool RgbwBleDriverManager::hostsAccessory(
    const RgbwBleDriverAdapter* adapter) const {
  if (adapter == nullptr) return false;
  for (const ChannelAssignment& assignment : accessoryAdapters_)
    if (assignment.adapter == adapter) return true;
  return false;
}

void RgbwBleDriverManager::begin() {
  const uint32_t now = millis();
  for (uint8_t index = 0; index < kAdapterCount; ++index) {
    desired_[index] = collectFor(adapters_[index]);
    changedMs_[index] = now;
    // Full-strip lights adopt the module's state on startup. Accessory
    // channels (pump, USB, accessories 3/4) instead start from the restored
    // safe state, so a pump left on before a restart is switched off.
    pending_[index] = hostsAccessory(adapters_[index]);
    if (adapters_[index] != nullptr) adapters_[index]->begin();
  }
}

bool RgbwBleDriverManager::update() {
  bool feedbackChanged = false;
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
      if (!equal(current, reported)) {
        const auto before = outputs_.status();
        applyReport(adapter, reported);
        feedbackChanged |= memcmp(&before, &outputs_.status(), sizeof(before)) != 0;
      }
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
  return feedbackChanged;
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
    // Only drive the outputs the user marked as wired for this light.
    const uint8_t wired = Sp630eChannels::capabilities(rgbwChannels_[zone]);
    const uint8_t white = output.rgbw[zone][3];
    uint8_t tones = output.rgbwOptions[zone] & 6;
    if ((wired & 6) && (tones & ~wired)) {
      // A requested white that is not wired uses the white that is.
      tones &= wired;
      if (!tones) tones = wired & 6;
    }
    if (wired & Sp630eChannels::kColour) memcpy(state.channels, output.rgbw[zone], 3);
    state.channels[3] = (tones & 2) ? white : 0;
    state.channels[4] = (tones & 4) ? white : 0;
    memcpy(state.color, output.rgb[zone], 3);
    state.brightness = output.rgbwBrightness[zone];
    state.options = (wired & output.rgbwOptions[zone] & 1) | (tones ? 2 : 0);
    if (!(wired & 6)) {
      // Group/scene white requests use RGB white on a strip without white.
      for (uint8_t channel = 0; channel < 3; ++channel)
        state.channels[channel] = std::max(state.channels[channel], white);
      state.channels[3] = state.channels[4] = 0;
      state.options =(state.channels[0] || state.channels[1] || state.channels[2]) ? 1 : 0;
    }
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
      // Feedback for outputs marked as not wired is not a light state.
      const uint8_t wired = Sp630eChannels::capabilities(rgbwChannels_[zone]);
      const uint8_t warm = (wired & Sp630eChannels::kWarmWhite) ? state.channels[3] : 0;
      const uint8_t cool = (wired & Sp630eChannels::kCoolWhite) ? state.channels[4] : 0;
      const bool anyOutput = state.channels[0] || state.channels[1] || state.channels[2] ||
                             warm || cool;
      // Channels first, then the preset, so OutputController's delayed
      // channel-to-preset conversion cannot overwrite the reported preset.
      bool channelsChanged = false;
      for (uint8_t channel = 0; channel < 4; ++channel) {
        const uint8_t level = channel == 3 ? std::max(warm, cool) : state.channels[channel];
        if (outputs_.status().rgbw[zone][channel] != level) {
          outputs_.setRgbwChannel(static_cast<RgbwZone>(zone), channel, level);
          channelsChanged = true;
        }
      }
      // Power-off reports can retain an unrelated hardware mode and zeroed
      // colour/brightness. Keep the user's preset for the next group toggle,
      // and cancel delayed channel-to-preset conversion after these writes.
      if (!anyOutput) {
        if (channelsChanged)
          outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), 4,
                                      outputs_.status().rgbwOptions[zone]);
        return;
      }
      const bool hasColour = state.color[0] || state.color[1] || state.color[2];
      for (uint8_t field = 0; field < 3; ++field) {
        const uint8_t selected = hasColour ? state.color[field] : outputs_.status().rgb[zone][field];
        outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), field, selected);
      }
      outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), 3, state.brightness);
      // A coexistence mode flag is capability, not an active RGB selection.
      const uint8_t colourOption =
          (state.channels[0] || state.channels[1] || state.channels[2]) ? 1 : 0;
      outputs_.setRgbwPresetField(static_cast<RgbwZone>(zone), 4,
          colourOption | (warm ? 2 : 0) | (cool ? 4 : 0));
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
