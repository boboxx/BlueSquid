#pragma once
#include <ArduinoJson.h>
#include "RvcFanProtocol.h"
#include <stdint.h>
#include <string.h>

namespace ConfigurationJson {
constexpr uint8_t version = 1;
inline bool validSections(JsonDocument& document) {
  return document["schema_version"].is<int>() && document["schema_version"].as<int>() == version &&
      document["settings"].is<JsonObjectConst>() && document["device"].is<JsonObjectConst>() &&
      document["outputs"].is<JsonObjectConst>();
}
inline bool readRvcFan(JsonObjectConst value, RvcFan::Config& config) {
  if (!value["enabled"].is<bool>() || !value["instance"].is<int>() ||
      !value["source_address"].is<int>()) return false;
  const int instance = value["instance"].as<int>(), source = value["source_address"].as<int>();
  if (instance < 1 || instance > 250 || source < 151 || source > 159) return false;
  config.enabled = value["enabled"].as<bool>(); config.instance = instance; config.source = source;
  return true;
}
inline bool readDevice(JsonObjectConst device, char* label, size_t capacity,
                       const char*& icon, uint8_t& colour, uint8_t colourCount) {
  const char* name = device["label"].as<const char*>();
  icon = device["icon"].as<const char*>();
  if (!name || !name[0] || strlen(name) >= capacity || !icon || !icon[0] ||
      !device["colour"].is<int>() || device["colour"].as<int>() < 0 ||
      device["colour"].as<int>() >= colourCount) return false;
  strcpy(label, name);
  colour = device["colour"].as<uint8_t>();
  return true;
}
inline bool percent(JsonVariantConst value, uint8_t& result) {
  if (!value.is<int>() || value.as<int>() < 0 || value.as<int>() > 100) return false;
  result = value.as<uint8_t>(); return true;
}
inline bool readLight(JsonObjectConst output, uint8_t (&levels)[4], uint8_t (&rgb)[3],
                      uint8_t& brightness, uint8_t& options) {
  JsonArrayConst values = output["output_rgbw_percent"].as<JsonArrayConst>();
  JsonArrayConst preset = output["preset_rgb_percent"].as<JsonArrayConst>();
  if (values.size() != 4 || preset.size() != 3 ||
      !percent(output["preset_brightness_percent"], brightness) ||
      !output["colour_selected"].is<bool>() || !output["warm_white_selected"].is<bool>() ||
      !output["cool_white_selected"].is<bool>()) return false;
  for (uint8_t i = 0; i < 4; ++i) if (!percent(values[i], levels[i])) return false;
  for (uint8_t i = 0; i < 3; ++i) if (!percent(preset[i], rgb[i])) return false;
  options = (output["colour_selected"].as<bool>() ? 1 : 0) |
      (output["warm_white_selected"].as<bool>() ? 2 : 0) |
      (output["cool_white_selected"].as<bool>() ? 4 : 0);
  return true;
}
}
