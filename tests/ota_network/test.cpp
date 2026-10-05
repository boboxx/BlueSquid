#include <assert.h>
#include <WiFi.h>
#include "CerboWifiManager.h"
static uint32_t now = 100;
uint32_t millis() { return now; }
int main() {
  SettingsManager settings;
  CerboWifiManager wifi(settings);
  assert(wifi.begin() && WiFi.ssid == "Cerbo");
  WiFi.connected = true; wifi.update(); assert(wifi.active());
  assert(!wifi.requestUpdateNetwork(true, 0));
  assert(wifi.requestUpdateNetwork(true, 1));
  wifi.update();
  assert(WiFi.ssid == "BlueSquid" && WiFi.password == "hotspotpassword");
  assert(!wifi.active() && settings.saves == 0);
  assert(wifi.updateNetworkStatus().phase == OtaLink::Phase::Joining);
  WiFi.connected = true; wifi.update();
  assert(wifi.updateNetworkStatus().phase == OtaLink::Phase::Ready);
  assert(wifi.updateNetworkStatus().address[3] == 2);
  assert(!wifi.active()); // Update Wi-Fi must not be reported as Cerbo connectivity.
  const auto starts = WiFi.starts;
  now += 10000; assert(wifi.requestUpdateNetwork(true, 1)); wifi.update();
  assert(WiFi.starts == starts);
  assert(wifi.requestUpdateNetwork(false, 99)); wifi.update();
  assert(WiFi.ssid == "BlueSquid"); // Stale cancellation cannot end this lease.
  assert(wifi.configure("NewCerbo", "newpassword")); wifi.update();
  assert(WiFi.ssid == "BlueSquid"); // Configuration waits until the update ends.
  assert(wifi.requestUpdateNetwork(false, 1)); wifi.update();
  assert(WiFi.ssid == "NewCerbo" && WiFi.password == "newpassword");
  assert(wifi.updateNetworkStatus().phase == OtaLink::Phase::Idle);
  assert(settings.saves == 1);
  assert(wifi.requestUpdateNetwork(true, 1)); wifi.update();
  assert(WiFi.ssid == "NewCerbo"); // Duplicate begin cannot resurrect a completed session.
  assert(wifi.requestUpdateNetwork(true, 2)); wifi.update();
  now += OtaLink::kJoinTimeoutMs; wifi.update();
  assert(WiFi.ssid == "NewCerbo" && wifi.updateNetworkStatus().phase == OtaLink::Phase::Failed);
  assert(wifi.requestUpdateNetwork(true, 3)); wifi.update();
  WiFi.connected = true; wifi.update();
  now += OtaLink::kLeaseMs; wifi.update();
  assert(WiFi.ssid == "NewCerbo" && wifi.updateNetworkStatus().phase == OtaLink::Phase::Failed);
  assert(settings.saves == 1); // Temporary OTA joins never persist over Cerbo settings.
}
