#pragma once

#include <stddef.h>
#include <stdint.h>

// Unencrypted BanlanX 6xx protocol, as documented by UniLED's
// custom_components/uniled/lib/ble/banlanx_6xx.py. Offsets include the header.
namespace Sp630eProtocol {
constexpr uint8_t kQuery[] = {0x53, 0x02, 0x00, 0x01, 0x00, 0x01, 0x01};
constexpr uint32_t kPollMs = 2000;
constexpr uint32_t kStaleMs = 8000;

// Let a queued command precede a routine poll, but bound deferral during
// continuous gestures so feedback and connection health still get serviced.
inline bool pollDue(uint32_t now, uint32_t lastQuery, bool commandPending) {
  return static_cast<uint32_t>(now - lastQuery) >=
      (commandPending ? 2 * kPollMs : kPollMs);
}

struct Status {
  bool power = false;
  uint8_t mode = 0;
  uint8_t configuration = 0;
  uint8_t channels[5]{}; // R, G, B, WW, CW
  uint8_t color[3]{};
  uint8_t brightness = 0;
  uint8_t options = 0;
};
inline uint8_t percent(uint8_t value) {
  return (static_cast<unsigned>(value) * 100U + 127U) / 255U;
}
inline bool fresh(bool valid, uint32_t received, uint32_t now) {
  return valid && static_cast<uint32_t>(now - received) < kStaleMs;
}
// Link health is independent of whether feedback can replace a newer command.
// Call received() only after successfully decoding a complete status packet.
struct ResponseHealth {
  bool valid = false;
  uint32_t lastReceivedMs = 0;
  void received(uint32_t now) { valid = true; lastReceivedMs = now; }
  void disconnected() { valid = false; }
  bool available(uint32_t now) const { return fresh(valid, lastReceivedMs, now); }
  uint32_t lastReplyOr(uint32_t connectedMs) const {
    return valid ? lastReceivedMs : connectedMs;
  }
};
inline bool decode(const uint8_t* data, size_t size, Status& output) {
  // Reject partial, encrypted, and unrelated packets before touching offsets.
  if (!data || size < 53 || data[0] != 0x53 || data[1] != 0x02 ||
      data[2] != 0 ||
      size != static_cast<size_t>(data[5]) + 6) return false;
  // Three-channel PWM RGB, RGBW, or five-channel PWM RGBCCT.
  if (data[19] != 0x85 && data[19] != 0x87 && data[19] != 0x88 && data[19] != 0x8A) return false;
  const uint8_t mode = data[32];
  if (mode < 1 || mode > 7) return false;
  Status next{};
  next.power = data[29] != 0;
  next.mode = mode;
  next.configuration = data[19];
  const bool rgbOnly = next.configuration == 0x85;
  const bool whiteMode = mode == 2 || mode == 4 || mode == 6;
  if (rgbOnly && whiteMode) return false;
  const bool coexist = !rgbOnly && mode <= 2 && data[24] != 0;
  const bool rgb = !whiteMode || coexist;
  const bool white = whiteMode || coexist;
  next.options = (rgb ? 1 : 0) | (white ? 2 : 0);
  const uint8_t colorLevel = mode == 5 ? 255 : data[35];
  const uint8_t whiteLevel = mode == 6 ? 255 : data[36];
  next.brightness = percent(whiteMode ? whiteLevel : colorLevel);
  for (uint8_t i = 0; i < 3; ++i) {
    const uint8_t color = data[(mode <= 2 ? 37 : 47) + i];
    next.color[i] = percent(color);
    if (next.power && rgb)
      next.channels[i] = (static_cast<uint32_t>(color) * colorLevel * 100U +
                          32512U) / 65025U;
  }
  if (next.configuration == 0x8A) {
    // RGBCCT coexistence uses a common colour brightness for all five
    // components. Otherwise white mode has its own brightness. Only the
    // warm component is wired: static WW is byte 41, effect WW is byte 51.
    const uint8_t warm = data[mode <= 2 ? 41 : 51];
    const uint8_t level = coexist ? colorLevel : whiteLevel;
    if (next.power && white)
      next.channels[4] = (static_cast<uint32_t>(data[mode <= 2 ? 40 : 50]) * level * 100U + 32512U) / 65025U;
    if (next.power && white)
      next.channels[3] = (static_cast<uint32_t>(warm) * level * 100U + 32512U) / 65025U;
    if (whiteMode && !coexist) {
      const uint8_t cold = data[mode <= 2 ? 40 : 50];
      const uint8_t component = warm > cold ? warm : cold;
      next.brightness = (static_cast<uint32_t>(component) * level * 100U + 32512U) / 65025U;
    }
  } else if (next.power && white) next.channels[3] = percent(whiteLevel);
  output = next;
  return true;
}
// Static five-channel PWM output. Full RGBW zones default CW to zero;
// individual assignments can pass a separate CW level.
// Coexistence applies one master brightness to RGB, CW and WW.
struct Packet { uint8_t data[10]{}; size_t size = 0; };
struct WarmCommands { Packet packets[6]{}; size_t count = 0; };
// Independent outputs stay in one static mode with fixed master levels.
// Zero channel values mean electrically off; avoid global power transitions
// when a relay or another shared assignment changes from/to zero.
inline WarmCommands independentCommands(const uint8_t channels[4], uint8_t cold, bool fiveChannel) {
  WarmCommands result{};
  const auto add = [&result](uint8_t opcode, const uint8_t* values, size_t size) {
    Packet& packet = result.packets[result.count++];
    packet.data[0] = 0x53; packet.data[1] = opcode;
    packet.data[3] = 1; packet.data[5] = size;
    for (size_t i = 0; i < size; ++i) packet.data[6 + i] = values[i];
    packet.size = size + 6;
  };
  const auto component = [](uint8_t level) -> uint8_t {
    return (level > 100 ? 100 : level) * 255U / 100U;
  };
  const uint8_t power[] = {1}, coexist[] = {1}, mode[] = {1, 1};
  add(0x50, power, 1);
  add(0x0A, coexist, 1);
  add(0x53, mode, 2);
  const uint8_t rgb[] = {component(channels[0]), component(channels[1]), component(channels[2]), 255};
  add(0x52, rgb, 4);
  if (fiveChannel) {
    const uint8_t cct[] = {component(cold), component(channels[3])};
    add(0x61, cct, 2);
  }
  const uint8_t white[] = {1, static_cast<uint8_t>(fiveChannel ? 255 : component(channels[3]))};
  add(0x51, white, 2);
  return result;
}
inline WarmCommands rgbWarmCommands(const uint8_t channels[4], uint8_t options, uint8_t cold = 0, bool independentChannels = false) {
  if (independentChannels) return independentCommands(channels, cold, true);
  uint8_t levels[5]{};
  uint8_t master = 0;
  for (uint8_t i = 0; i < 5; ++i) {
    levels[i] = (options & (i >= 3 ? 2 : 1)) ? (i == 4 ? cold : channels[i]) : 0;
    if (levels[i] > 100) levels[i] = 100;
    if (levels[i] > master) master = levels[i];
  }
  WarmCommands result;
  const auto add = [&result](uint8_t opcode, const uint8_t* values, size_t size) {
    Packet& packet = result.packets[result.count++];
    packet.data[0] = 0x53; packet.data[1] = opcode;
    packet.data[3] = 1; packet.data[5] = size;
    for (size_t i = 0; i < size; ++i) packet.data[6 + i] = values[i];
    packet.size = size + 6;
  };
  const uint8_t power[] = {static_cast<uint8_t>(master != 0)};
  add(0x50, power, 1);
  if (!master) return result;
  const bool rgbOn = levels[0] || levels[1] || levels[2];
  if (!rgbOn) {
    // White-only uses the peripheral's dedicated white mode. Sending 0x52
    // here needlessly touches its saved colour/master-brightness registers.
    const uint8_t coexist[] = {0};
    add(0x0A, coexist, 1);
    const uint8_t mode[] = {2, 1};
    add(0x53, mode, 2);
    const uint8_t cct[] = {static_cast<uint8_t>(levels[4] * 255U / master),
                           static_cast<uint8_t>(levels[3] * 255U / master)};
    add(0x61, cct, 2);
    const uint8_t brightness[] = {1, static_cast<uint8_t>(master * 255U / 100U)};
    add(0x51, brightness, 2);
    return result;
  }
  const bool whiteOn = levels[3] || levels[4];
  const uint8_t coexist[] = {static_cast<uint8_t>(whiteOn)};
  add(0x0A, coexist, 1);
  const uint8_t mode[] = {1, 1};
  add(0x53, mode, 2);
  uint8_t rgb[4]{};
  for (uint8_t i = 0; i < 3; ++i) rgb[i] = levels[i] * 255U / master;
  rgb[3] = master * 255U / 100U;
  add(0x52, rgb, 4);
  // Colour-only must not touch white registers or enable combined mode.
  if (!whiteOn) return result;
  const uint8_t cct[] = {static_cast<uint8_t>(levels[4] * 255U / master),
                         static_cast<uint8_t>(levels[3] * 255U / master)};
  add(0x61, cct, 2);
  // 0x52 updates RGB brightness only. Keep the separate white brightness
  // register in step with the normalized WW component, including WW-only
  // dimming where that component stays at 255 throughout the gesture.
  const uint8_t whiteBrightness[] = {1, rgb[3]};
  add(0x51, whiteBrightness, 2);
  return result;
}
// Four-channel RGBW devices also need a white-only path that never writes RGB.
// The shared cache planner applies setup after power-on.
inline WarmCommands rgbwCommands(const uint8_t channels[4], uint8_t options, bool independentChannels = false) {
  if (independentChannels) return independentCommands(channels, 0, false);
  const bool rgbOn = (options & 1) && (channels[0] || channels[1] || channels[2]);
  const bool whiteOn = (options & 2) && channels[3];
  WarmCommands result{};
  const auto add = [&result](uint8_t opcode, const uint8_t* values, size_t size) {
    Packet& packet = result.packets[result.count++];
    packet.data[0] = 0x53; packet.data[1] = opcode;
    packet.data[3] = 1; packet.data[5] = size;
    for (size_t i = 0; i < size; ++i) packet.data[6 + i] = values[i];
    packet.size = size + 6;
  };
  const uint8_t power[] = {static_cast<uint8_t>(rgbOn || whiteOn)};
  add(0x50, power, 1);
  if (!power[0]) return result;
  const uint8_t coexist[] = {static_cast<uint8_t>(rgbOn && whiteOn)};
  add(0x0A, coexist, 1);
  const uint8_t mode[] = {static_cast<uint8_t>(rgbOn ? 1 : 2), 1};
  add(0x53, mode, 2);
  if (rgbOn) {
    uint8_t level = channels[0];
    for (uint8_t i = 1; i < 3; ++i) if (channels[i] > level) level = channels[i];
    const uint8_t rgb[] = {static_cast<uint8_t>(channels[0] * 255U / level),
                           static_cast<uint8_t>(channels[1] * 255U / level),
                           static_cast<uint8_t>(channels[2] * 255U / level),
                           static_cast<uint8_t>(level * 255U / 100U)};
    add(0x52, rgb, 4);
  }
  if (whiteOn) {
    const uint8_t white[] = {1, static_cast<uint8_t>(channels[3] * 255U / 100U)};
    add(0x51, white, 2);
  }
  return result;
}
// PWM RGB has no white or coexistence controls.
inline WarmCommands rgbCommands(const uint8_t channels[5]) {
  const uint8_t rgb[4] = {channels[0], channels[1], channels[2], 0};
  const auto combined = rgbwCommands(rgb, 1);
  WarmCommands result{};
  for (size_t i = 0; i < combined.count; ++i) {
    if (combined.packets[i].data[1] != 0x0A)
      result.packets[result.count++] = combined.packets[i];
  }
  return result;
}
// Cache only acknowledged writes. Unknown/reconnected state requires full setup.
struct WarmCommandCache {
  WarmCommands previous{};
  bool valid = false;
  void reset() { valid = false; }
  // Reconcile an accepted status so app-side changes cannot leave a stale
  // write cache (especially power, coexistence, or mode).
  void observe(const uint8_t* data) {
    if (!valid) return;
    for (size_t i = 0; i < previous.count; ++i) {
      Packet& p = previous.packets[i];
      if (!p.size) continue;
      bool matches = true;
      switch (p.data[1]) {
        case 0x50: matches = p.data[6] == data[29]; break;
        case 0x0A: matches = p.data[6] == data[24]; break;
        case 0x53: matches = p.data[6] == data[32]; break;
        case 0x52:
          matches = p.data[6] == data[37] && p.data[7] == data[38] &&
                    p.data[8] == data[39] && p.data[9] == data[35]; break;
        case 0x51: matches = p.data[7] == data[36]; break;
        case 0x61: matches = p.data[6] == data[40] && p.data[7] == data[41]; break;
      }
      // Invalidate only the changed register. Replaying an unchanged power
      // command can restart the peripheral's on/off transition effect.
      if (!matches) p.size = 0;
    }
  }
  WarmCommands plan(const WarmCommands& next) const {
    WarmCommands result{};
    if (!next.count) return result;
    const auto matches = [this](const Packet& packet) {
      if (!valid) return false;
      for (size_t j = 0; j < previous.count; ++j) {
        const Packet& old = previous.packets[j];
        if (old.size != packet.size || old.data[1] != packet.data[1]) continue;
        for (size_t k = 0; k < packet.size; ++k)
          if (old.data[k] != packet.data[k]) return false;
        return true;
      }
      return false;
    };
    const Packet& power = next.packets[0];
    if (power.data[1] == 0x50 && power.data[6] == 0) {
      if (matches(power)) return result;
      // Clear retained output levels while the peripheral is still awake.
      // Otherwise waking it for one assignment can flash another assignment's
      // old level before the new channel packet arrives. Keep UI presets local.
      Packet rgb{};
      rgb.data[0] = 0x53; rgb.data[1] = 0x52;
      rgb.data[3] = 1; rgb.data[5] = 4; rgb.size = 10;
      result.packets[result.count++] = rgb;
      Packet white{};
      white.data[0] = 0x53; white.data[1] = 0x51;
      white.data[3] = 1; white.data[5] = 2;
      white.data[6] = 1; white.size = 8;
      result.packets[result.count++] = white;
      result.packets[result.count++] = power;
      return result;
    }
    const bool waking = power.data[1] == 0x50 && power.data[6] == 1 && !matches(power);
    // Captured feedback shows a pre-power mode write does not survive wake.
    // Power up first, then reapply every requested setting, even when cached.
    // Steady-state dimming still sends only changed registers and no power.
    for (size_t i = 0; i < next.count; ++i) {
      const Packet& packet = next.packets[i];
      if (waking || !matches(packet)) result.packets[result.count++] = packet;
    }
    return result;
  }
  void committed(const WarmCommands& next) {
    // Shutdown clears physical levels; retain only the requested off state.
    // Every wake replays the complete requested setup after power-on.
    previous = next;
    valid = true;
  }
};
}  // namespace Sp630eProtocol
