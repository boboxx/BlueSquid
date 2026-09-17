#include "TouchHotspot.h"
#include "WifiDefaults.h"
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "WebRemotePage.h"

namespace TouchHotspot {
namespace {
struct Request { char ssid[33]; char password[64]; };
WebServer server(80);
StatusProvider statusProvider=nullptr;
CommandHandler commandHandler=nullptr;
String token;
QueueHandle_t requests = nullptr;
Preferences preferences;
Status current;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
String savedSsid, savedPassword;
bool active = false;
void publish() {
  Status next{};
  strlcpy(next.ssid, savedSsid.c_str(), sizeof(next.ssid));
  strlcpy(next.ip, active ? WiFi.softAPIP().toString().c_str() : "Unavailable", sizeof(next.ip));
  next.active = active;
  next.clients = active ? WiFi.softAPgetStationNum() : 0;
  portENTER_CRITICAL(&mux); current = next; portEXIT_CRITICAL(&mux);
}
void start() {
  WiFi.mode(WIFI_AP);
  active = WiFi.softAP(savedSsid.c_str(), savedPassword.c_str());
  Serial.printf("System hotspot: %s\n", active ? "started" : "failed");
  publish();
}
}
void setRemoteHandlers(StatusProvider status, CommandHandler command) {
  statusProvider=status; commandHandler=command;
}
bool begin() {
  requests = xQueueCreate(1, sizeof(Request));
  if (!requests || !preferences.begin("system-hotspot", false)) return false;
  savedSsid = preferences.getString("ssid", WifiDefaults::hotspotSsid);
  savedPassword = preferences.getString("password", WifiDefaults::hotspotPassword);
  WiFi.persistent(false);
  start();
  char secret[33];
  snprintf(secret,sizeof(secret),"%08lx%08lx%08lx%08lx",
      (unsigned long)esp_random(),(unsigned long)esp_random(),
      (unsigned long)esp_random(),(unsigned long)esp_random());
  token=secret;
  const char* headers[]={"X-BlueSquid-Token"}; server.collectHeaders(headers,1);
  server.on("/",HTTP_GET,[]{server.send_P(200,"text/html",kWebRemotePage);});
  server.on("/api/status",HTTP_GET,[]{
    JsonDocument doc;
    if (statusProvider) deserializeJson(doc,statusProvider());
    doc["token"]=token;
    String body; serializeJson(doc,body);
    server.sendHeader("Cache-Control","no-store");
    server.send(200,"application/json",body);
  });
  server.on("/api/command",HTTP_POST,[]{
    if (server.header("X-BlueSquid-Token")!=token) {server.send(403);return;}
    const String target=server.arg("target"), value=server.arg("value");
    if (target.isEmpty()||value.isEmpty()||target.length()>1||value.length()>8) {server.send(400);return;}
    for(char c:target) if(c<'0'||c>'9'){server.send(400);return;}
    for(char c:value) if(c<'0'||c>'9'){server.send(400);return;}
    const bool sent=commandHandler && commandHandler(server.arg("kind"),target.toInt(),strtoul(value.c_str(),nullptr,10));
    server.send(sent?202:409,"text/plain",sent?"Queued":"Unavailable or invalid command");
  });
  server.onNotFound([]{server.send(404);});
  server.begin();
  return active;
}
bool configure(const String& ssid, const String& password) {
  if (!requests || ssid.isEmpty() || ssid.length()>32 || password.length()<8 || password.length()>63) return false;
  Request request{};
  strlcpy(request.ssid, ssid.c_str(), sizeof(request.ssid));
  strlcpy(request.password, password.c_str(), sizeof(request.password));
  return xQueueOverwrite(requests, &request)==pdTRUE;
}
void update() {
  server.handleClient();
  Request request{};
  if (requests && xQueueReceive(requests, &request, 0)==pdTRUE) {
    const String ssid(request.ssid), password(request.password);
    if (preferences.putString("ssid", ssid)==ssid.length() &&
        preferences.putString("password", password)==password.length()) {
      savedSsid=ssid; savedPassword=password;
      WiFi.softAPdisconnect(false);
      start();
    } else Serial.println("System hotspot: failed to save settings");
  }
  publish();
}
Status status() {
  portENTER_CRITICAL(&mux); const Status copy=current; portEXIT_CRITICAL(&mux);
  return copy;
}
}
