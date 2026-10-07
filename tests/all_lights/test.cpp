#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "AppConfig.h"
#include "LightSwitchManager.h"
#include "RgbwBleDriverManager.h"
#include "Sp630eChannels.h"
#include "../../src/touchscreen/TouchRemoteStatus.h"

static_assert(sizeof(PersistentDeviceState) == 41, "Keep saved device records readable");
static_assert(offsetof(PersistentDeviceState, usbEnabled) == 37, "Preserve saved USB field offset");
static_assert(offsetof(PersistentDeviceState, accessory4Requested) == 40, "Preserve saved accessory offset");

uint32_t nowMs = 1000;
int switchLevel = HIGH;
uint32_t millis() { return nowMs; }
int digitalRead(int pin) { assert(pin == 35); return switchLevel; }
void pinMode(int pin, int mode) {
  if (pin == 35) assert(mode == INPUT_PULLUP);
}
uint8_t SettingsManager::loadLightGroupMask() { return 15; }
PersistentDeviceState savedState;
bool restoreSaved = false;
bool SettingsManager::loadDeviceState(PersistentDeviceState& state) {
  if (restoreSaved) state = savedState;
  return restoreSaved;
}
bool SettingsManager::saveDeviceState(const PersistentDeviceState& state) {
  savedState = state; return true;
}

struct GroupAdapter : RgbwBleDriverAdapter {
  RgbwBleDriverState sent{}, feedback{};
  uint32_t revision = 0;
  bool valid = false;
  void begin() override {}
  void update() override {}
  bool ready() const override { return true; }
  bool available() const override { return true; }
  bool send(const RgbwBleDriverState& state) override {
    sent = state; valid = false; return true;
  }
  bool reported(RgbwBleDriverState& state, uint32_t& rev) const override {
    state = feedback; rev = revision; return valid;
  }
  void reportOff() {
    feedback = {}; feedback.options = 1; // Hardware off mode is RGB, even after CW.
    valid = true; ++revision;
  }
};

void press(LightSwitchManager& button);

void groupPreservesSelectedChannels() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  GroupAdapter rgb, cct, excluded;
  RgbwBleDriverManager manager(outputs);
  manager.setAdapter(0, &rgb, Sp630eChannels::rgbOnly);
  manager.setAdapter(1, &cct);
  manager.setAdapter(2, &excluded, 1); // A single-channel assignment outside group.
  outputs.setLightGroupMask(3);
  const uint8_t off[4]{};
  const uint8_t colour[3] = {100, 25, 50};
  outputs.setRgbwState(RgbwZone::Output1, off, colour, 20, 1);
  outputs.setRgbwState(RgbwZone::Output2, off, colour, 35, 4);
  manager.begin();
  auto flush = [&]() { manager.update(); nowMs += 150; manager.update(); };
  // Dashboard and physical switch invoke the same group command.
  outputs.setAllLightsEnabled(true); flush();
  assert(rgb.sent.channels[0] == 100 && rgb.sent.channels[1] == 25);
  assert(rgb.sent.channels[2] == 50 && rgb.sent.channels[3] == 0);
  assert(cct.sent.channels[4] == 100 && cct.sent.channels[3] == 0);
  assert(cct.sent.channels[0] == 0 && excluded.sent.channels[1] == 0);
  LightSwitchManager button(outputs); button.begin(); press(button); flush();
  assert(!outputs.anyLightsEnabled());
  rgb.reportOff(); cct.reportOff(); manager.update();
  assert(outputs.status().rgbwOptions[1] == 4);
  press(button); flush();
  assert(rgb.sent.channels[1] == 25 && cct.sent.channels[4] == 100);
  assert(cct.sent.channels[3] == 0);
  // RGB + CW stays selected, with no unexpected WW output.
  outputs.setRgbwPresetField(RgbwZone::Output2, 4, 5);
  outputs.setAllLightsEnabled(false); flush();
  cct.reportOff(); manager.update();
  outputs.setAllLightsEnabled(true); flush();
  assert(cct.sent.channels[0] == 100 && cct.sent.channels[1] == 25);
  assert(cct.sent.channels[4] == 100 && cct.sent.channels[3] == 0);
  // Individual UI off clears active option bits; remember the selection,
  // including across device-state persistence, for the next group command.
  outputs.setRgbwState(RgbwZone::Output2, off, colour, 35, 0); flush();
  cct.reportOff(); manager.update();
  nowMs += 3100; outputs.update();
  assert(savedState.rgbwOptions[1] == 5);
  restoreSaved = true;
  OutputController rebooted(events, settings); rebooted.begin();
  restoreSaved = false;
  rebooted.setRgbwExternal(1, true); rebooted.setLightGroupMask(2);
  rebooted.setAllLightsEnabled(true);
  assert(rebooted.status().rgbw[1][0] == 100 && rebooted.status().rgbw[1][3] == 100);
  assert(rebooted.status().rgbwOptions[1] == 5);
}

void channelCommandKeepsWhiteTone() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  const uint8_t off[4]{};
  const uint8_t colour[3]{};
  outputs.setRgbwState(RgbwZone::Output1, off, colour, 50, 4);  // Cool white.
  // Per-channel commands and scenes set only the shared white channel.
  outputs.setRgbwChannel(RgbwZone::Output1, 3, 60);
  nowMs += AppConfig::kRgbwCommandSettleMs; outputs.update();
  assert(outputs.status().rgbwOptions[0] == 4);
}

void press(LightSwitchManager& button) {
  switchLevel = LOW;
  assert(!button.update());
  nowMs += 49; assert(!button.update());
  nowMs += 1; assert(button.update());
  nowMs += 1000; assert(!button.update()); // held switch cannot repeat
  switchLevel = HIGH; assert(!button.update());
  nowMs += 50; assert(!button.update()); // release cannot toggle
}

void defaultColourIsWhite() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  for (uint8_t zone = 0; zone < 4; ++zone)
    for (uint8_t channel = 0; channel < 3; ++channel)
      assert(outputs.status().rgb[zone][channel] == 100);
}

void holdOpensPairing() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true); outputs.setLightGroupMask(1);
  LightSwitchManager button(outputs); button.begin();
  switchLevel = LOW; button.update();
  nowMs += 50; assert(button.update() && outputs.anyLightsEnabled());
  assert(!button.consumePairingRequest());
  nowMs += AppConfig::Outputs::kPairingHoldMs - 1; assert(!button.update());
  nowMs += 1; assert(button.update());
  // The hold restores the lights and requests pairing exactly once.
  assert(!outputs.anyLightsEnabled());
  assert(button.consumePairingRequest() && !button.consumePairingRequest());
  nowMs += 10000; assert(!button.update());
  switchLevel = HIGH; button.update(); nowMs += 50; assert(!button.update());
  // A normal press afterwards still toggles.
  press(button); assert(outputs.anyLightsEnabled());
}

void lightGroupToggle() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings);
  outputs.begin();
  outputs.setRgbwExternal(0, true); outputs.setRgbwExternal(1, true);
  outputs.setRgbwPresetField(RgbwZone::Output1, 3, 12);
  outputs.setRgbwPresetField(RgbwZone::Output2, 3, 34);
  outputs.setUsbEnabled(true); outputs.setWaterPumpEnabled(true);
  outputs.setAccessory3Enabled(true);
  outputs.setRvcFanStatus(true, true, true, 43, false, 159, 1, 0, 0);
  LightSwitchManager button(outputs); button.begin();
  press(button);
  const auto& s = outputs.status();
  assert(s.rgbw[0][3] == 100 && s.rgbw[1][3] == 100);
  assert(s.rgbw[0][0] == 0 && s.rgbw[1][0] == 0 && s.rgbwBrightness[0] == 100);
  assert(s.usbEnabled && s.waterPumpEnabled && s.accessory3Enabled && s.fanSpeed == 43);
  press(button); assert(!outputs.anyLightsEnabled());
  // Only a colour output is on. The GPIO button must turn it off, keep the group off.
  outputs.setRgbwChannel(RgbwZone::Output1, 0, 27);
  press(button); assert(!outputs.anyLightsEnabled());
  // Contact bounce shorter than the debounce threshold must not trigger.
  switchLevel = LOW; assert(!button.update()); nowMs += 20;
  switchLevel = HIGH; assert(!button.update()); nowMs += 100;
  assert(!button.update() && !outputs.anyLightsEnabled());
}

void allFourRgbwLights() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true); outputs.setRgbwExternal(1, true);
  outputs.setRgbwExternal(2, true); outputs.setRgbwExternal(3, true);
  outputs.setRgbwPresetField(RgbwZone::Output3, 4, 4); // CW-only installation
  outputs.setRgbwPresetField(RgbwZone::Output4, 4, 6); // WW + CW
  LightSwitchManager button(outputs); button.begin(); press(button);
  const auto& s = outputs.status();
  for (uint8_t z=0; z<2; ++z) {
    assert(s.rgbw[z+2][0] == 0 && s.rgbw[z+2][3] == 100);
    assert(s.rgbwBrightness[z+2] == 100);
  }
  assert(s.rgbwOptions[2] == 4 && s.rgbwOptions[3] == 6);
  assert(s.rgbw[0][3] == 100 && s.rgbw[1][3] == 100);
  press(button); assert(!outputs.anyLightsEnabled());
  outputs.setRgbwChannel(RgbwZone::Output4, 1, 15);
  press(button); assert(!outputs.anyLightsEnabled());
  nowMs += 200; outputs.update();
  assert(!outputs.anyLightsEnabled());
}

void unassignedRgbwDoesNotHoldToggleOn() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true);
  // Stale state from a removed assignment must not count as a physical light.
  outputs.setRgbwChannel(RgbwZone::Output2, 3, 80);
  LightSwitchManager button(outputs); button.begin();
  press(button);
  assert(outputs.status().rgbw[0][3] == 100);
  assert(outputs.status().rgbw[1][3] == 80); // unassigned slot is not commanded
  outputs.setRgbwChannel(RgbwZone::Output1, 3, 0);
  assert(!outputs.anyLightsEnabled());
  press(button); assert(outputs.status().rgbw[0][3] == 100);
}

void homeGroupStatusAndPhysicalToggle() {
  TouchRemoteStatus remote;
  remote.rgbw[1][3] = 80; // removed assignment is not part of the group
  assert(!remote.anyLightsEnabled());
  remote.sp630eAssigned = 1;
  remote.rgbw[0][0] = 25;
  assert(remote.anyLightsEnabled());
  remote.rgbw[0][0] = 0;
  assert(!remote.anyLightsEnabled());
  for (uint8_t zone = 0; zone < 4; ++zone) {
    remote = {};
    remote.sp630eAssigned = 1U << zone;
    remote.rgbwChannels(zone)[3] = 100;
    assert(remote.anyLightsEnabled());
  }
  remote = {}; remote.usb = true; remote.fan = 100;
  assert(!remote.anyLightsEnabled());

  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true);
  LightSwitchManager button(outputs); button.begin();
  // Home transport handlers invoke this same action. GPIO must see that state.
  outputs.setAllLightsEnabled(true);
  assert(outputs.status().rgbw[0][3] == 100);
  press(button); assert(!outputs.anyLightsEnabled());
  press(button); assert(outputs.status().rgbw[0][3] == 100);
  outputs.setAllLightsEnabled(false);
  assert(!outputs.anyLightsEnabled());
  press(button); assert(outputs.status().rgbw[0][3] == 100);
}

void configurableGroup() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true); outputs.setRgbwExternal(1, true);
  outputs.setRgbwChannel(RgbwZone::Output2, 3, 42);
  outputs.setRgbwExternal(2, true); outputs.setRgbwExternal(3, true);
  outputs.setRgbwChannel(RgbwZone::Output4, 3, 27);
  outputs.setLightGroupMask(5); // RGBW lights 1 and 3.
  assert(!outputs.anyLightsEnabled()); // excluded lights do not affect toggle
  outputs.setAllLightsEnabled(true);
  assert(outputs.status().rgbw[0][3] == 100 && outputs.status().rgbw[2][3] == 100);
  assert(outputs.status().rgbw[1][3] == 42 && outputs.status().rgbw[3][3] == 27);
  outputs.setAllLightsEnabled(false);
  assert(!outputs.anyLightsEnabled());
  assert(outputs.status().rgbw[1][3] == 42 && outputs.status().rgbw[3][3] == 27);
  outputs.setLightGroupMask(0); outputs.setAllLightsEnabled(true);
  assert(!outputs.anyLightsEnabled() && outputs.status().rgbw[0][3] == 0);
  TouchRemoteStatus remote; remote.sp630eAssigned = 3; remote.rgbw[1][3] = 42;
  assert(!remote.anyLightsEnabled(5)); assert(remote.anyLightsEnabled(2));
}

void configurationBatchValidation() {
  Sp630eAssignment rows[8]{}; uint8_t group = 15;
  const char* valid = "5|0,1,AA:BB:CC:DD:EE:FF;1,0,AA:BB:CC:DD:EE:FF;2,0,none;3,0,none;4,255,none;5,0,none;6,0,none;7,0,none;";
  assert(parseSp630eConfiguration(valid, rows, group));
  assert(group == 5 && rows[0].channel == 1 && rows[1].channel == 0);
  const char* conflict = "5|0,255,AA:BB:CC:DD:EE:FF;1,0,AA:BB:CC:DD:EE:FF;2,0,none;3,0,none;4,0,none;5,0,none;6,0,none;7,0,none;";
  assert(!parseSp630eConfiguration(conflict, rows, group));
  assert(rows[0].channel == 1); // rejected batch cannot partially apply
  assert(!parseSp630eConfiguration("16|", rows, group));
  assert(!parseSp630eConfiguration("1|0,0,invalid;", rows, group));
  assert(!parseSp630eConfiguration("1|0,0,none;", rows, group));
  // Full strips with chosen outputs (here RGB + cool white, 0xF5 = 245).
  const char* outputs = "1|0,245,AA:BB:CC:DD:EE:FF;1,0,none;2,0,none;3,0,none;4,0,none;5,0,none;6,0,none;7,0,none;";
  assert(parseSp630eConfiguration(outputs, rows, group) && rows[0].channel == 245);
  // An accessory can never take a full-strip assignment.
  const char* accessory = "1|0,0,none;1,0,none;2,0,none;3,0,none;4,245,AA:BB:CC:DD:EE:FF;5,0,none;6,0,none;7,0,none;";
  assert(!parseSp630eConfiguration(accessory, rows, group));
}

void fanRequiresRvcHandler() {
  EventManager events; SettingsManager settings;
  OutputController outputs(events, settings);
  unsigned calls = 0;
  outputs.setFanCommandHandler([&](uint8_t speed) { ++calls; return speed == 40; });
  outputs.begin();
  assert(calls == 0); // Startup never restores or transmits fan power.
  assert(outputs.setFanSpeed(40) && calls == 1);
  assert(!outputs.setFanSpeed(101) && calls == 1);
  outputs.setFanCommandHandler({});
  assert(!outputs.setFanSpeed(40));
  assert(!outputs.setFanReverse(true));
}

int main() { groupPreservesSelectedChannels(); channelCommandKeepsWhiteTone(); holdOpensPairing(); defaultColourIsWhite(); fanRequiresRvcHandler(); configurableGroup(); configurationBatchValidation();
  homeGroupStatusAndPhysicalToggle();
  lightGroupToggle(); allFourRgbwLights(); unassignedRgbwDoesNotHoldToggleOn();
  puts("GPIO 35 all-lights tests passed");
}
