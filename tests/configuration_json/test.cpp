#include <assert.h>
#include "ConfigurationJson.h"
int main() {
  JsonDocument d;
  deserializeJson(d, R"({"schema_version":1,"settings":{},"device":{"rgbw_1":{"label":"Front","icon":"light","colour":11}},"outputs":{}})");
  assert(ConfigurationJson::validSections(d));
  for (int old : {0, 4, 5}) { d["schema_version"] = old; assert(!ConfigurationJson::validSections(d)); }
  d["schema_version"] = 1;
  char name[32]; const char* icon = nullptr; uint8_t colour;
  auto device = d["device"]["rgbw_1"].as<JsonObject>();
  assert(ConfigurationJson::readDevice(device, name, sizeof name, icon, colour, 21));
  assert(!strcmp(name,"Front") && !strcmp(icon,"light") && colour == 11);
  device["colour"] = 21; assert(!ConfigurationJson::readDevice(device, name, sizeof name, icon, colour, 21));
  device.remove("colour"); assert(!ConfigurationJson::readDevice(device, name, sizeof name, icon, colour, 21));
  JsonDocument fan;
  deserializeJson(fan, R"({"enabled":true,"instance":1,"source_address":159})");
  RvcFan::Config rvc;
  assert(ConfigurationJson::readRvcFan(fan.as<JsonObjectConst>(), rvc));
  assert(rvc.enabled && rvc.instance == 1 && rvc.source == 159);
  fan["instance"] = 0; assert(!ConfigurationJson::readRvcFan(fan.as<JsonObjectConst>(), rvc));
  fan["instance"] = 251; assert(!ConfigurationJson::readRvcFan(fan.as<JsonObjectConst>(), rvc));
  fan["instance"] = 1; fan["source_address"] = 142;
  assert(!ConfigurationJson::readRvcFan(fan.as<JsonObjectConst>(), rvc));
  fan["source_address"] = 159; fan["enabled"] = "true";
  assert(!ConfigurationJson::readRvcFan(fan.as<JsonObjectConst>(), rvc));
  JsonDocument light;
  deserializeJson(light, R"({"output_rgbw_percent":[0,0,0,65],"preset_rgb_percent":[100,0,0],"preset_brightness_percent":65,"colour_selected":false,"warm_white_selected":false,"cool_white_selected":true})");
  uint8_t levels[4], rgb[3], brightness, options;
  assert(ConfigurationJson::readLight(light.as<JsonObjectConst>(),levels,rgb,brightness,options));
  assert(options == 4 && levels[3] == 65 && brightness == 65);
  light["warm_white_selected"] = true;
  assert(ConfigurationJson::readLight(light.as<JsonObjectConst>(),levels,rgb,brightness,options) && options == 6);
  light["output_rgbw_percent"][0] = 101;
  assert(!ConfigurationJson::readLight(light.as<JsonObjectConst>(),levels,rgb,brightness,options));
}
