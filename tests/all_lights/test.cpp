#include <assert.h>
#include <stdio.h>
#include "AppConfig.h"
#include "LightSwitchManager.h"
#include "../../src/touchscreen/TouchRemoteStatus.h"

uint32_t nowMs = 1000;
int switchLevel = HIGH;
bool pwmPresent = true;
uint32_t millis() { return nowMs; }
int digitalRead(int pin) { assert(pin == 35); return switchLevel; }
void pinMode(int pin, int mode) {
  if (pin == 35) assert(mode == INPUT_PULLUP);
}
PwmManager::PwmManager(uint8_t address) : driver_(address) {}
bool PwmManager::setPercent(uint8_t, uint8_t) { return pwmPresent; }
uint8_t SettingsManager::loadLightGroupMask() { return 15; }
bool SettingsManager::loadDeviceState(PersistentDeviceState&) { return false; }
bool SettingsManager::saveDeviceState(const PersistentDeviceState&) { return true; }

void press(LightSwitchManager& button) {
  switchLevel = LOW;
  assert(!button.update());
  nowMs += 49; assert(!button.update());
  nowMs += 1; assert(button.update());
  nowMs += 1000; assert(!button.update()); // held switch cannot repeat
  switchLevel = HIGH; assert(!button.update());
  nowMs += 50; assert(!button.update()); // release cannot toggle
}

void lightGroupToggle() {
  PwmManager pwm(0x40); EventManager events; SettingsManager settings;
  OutputController outputs(pwm, events, settings);
  outputs.begin();
  outputs.setRgbwExternal(0, true); outputs.setRgbwExternal(1, true);
  outputs.setRgbwPresetField(RgbwZone::Output1, 3, 12);
  outputs.setRgbwPresetField(RgbwZone::Output2, 3, 34);
  outputs.setUsbEnabled(true); outputs.setWaterPumpEnabled(true);
  outputs.setAccessory3Enabled(true); outputs.setFanSpeed(43);
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
  PwmManager pwm(0x40); EventManager events; SettingsManager settings;
  OutputController outputs(pwm, events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true); outputs.setRgbwExternal(1, true);
  outputs.setRgbwExternal(2, true); outputs.setRgbwExternal(3, true);
  outputs.setRgbwPresetField(RgbwZone::Output3, 4, 4); // CW-only installation
  outputs.setRgbwPresetField(RgbwZone::Output4, 4, 6); // WW + CW
  pwmPresent = false; // missing PCA9685 cannot prevent SP630E light commands
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
  pwmPresent = true;
}

void unassignedRgbwDoesNotHoldToggleOn() {
  PwmManager pwm(0x40); EventManager events; SettingsManager settings;
  OutputController outputs(pwm, events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true);
  // Stale state from a removed assignment must not count as a physical light.
  outputs.setRgbwChannel(RgbwZone::Output2, 3, 80);
  pwmPresent = false;
  LightSwitchManager button(outputs); button.begin();
  press(button);
  assert(outputs.status().rgbw[0][3] == 100);
  assert(outputs.status().rgbw[1][3] == 80); // unassigned slot is not commanded
  outputs.setRgbwChannel(RgbwZone::Output1, 3, 0);
  assert(!outputs.anyLightsEnabled());
  press(button); assert(outputs.status().rgbw[0][3] == 100);
  pwmPresent = true;
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

  PwmManager pwm(0x40); EventManager events; SettingsManager settings;
  OutputController outputs(pwm, events, settings); outputs.begin();
  outputs.setRgbwExternal(0, true);
  pwmPresent = false;
  LightSwitchManager button(outputs); button.begin();
  // Home transport handlers invoke this same action. GPIO must see that state.
  outputs.setAllLightsEnabled(true);
  assert(outputs.status().rgbw[0][3] == 100);
  press(button); assert(!outputs.anyLightsEnabled());
  press(button); assert(outputs.status().rgbw[0][3] == 100);
  outputs.setAllLightsEnabled(false);
  assert(!outputs.anyLightsEnabled());
  press(button); assert(outputs.status().rgbw[0][3] == 100);
  pwmPresent = true;
}

void configurableGroup() {
  PwmManager pwm(0x40); EventManager events; SettingsManager settings;
  OutputController outputs(pwm, events, settings); outputs.begin();
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
}

int main() { configurableGroup(); configurationBatchValidation();
  homeGroupStatusAndPhysicalToggle();
  lightGroupToggle(); allFourRgbwLights(); unassignedRgbwDoesNotHoldToggleOn();
  puts("GPIO 35 all-lights tests passed");
}
