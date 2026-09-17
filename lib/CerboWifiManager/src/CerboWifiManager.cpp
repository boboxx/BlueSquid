#include "CerboWifiManager.h"
#include <WiFi.h>
#include "Logging.h"
#include "WifiDefaults.h"

namespace { constexpr char kTag[] = "CerboWiFi"; }

bool CerboWifiManager::begin() {
  // Separate keys deliberately avoid reusing the old Controller AP credentials.
  ssid_ = settings_.loadCerboStationSsid(WifiDefaults::cerboSsid);
  password_ = settings_.loadCerboStationPassword(WifiDefaults::cerboPassword);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  return startStation();
}

bool CerboWifiManager::startStation() {
  lastAttemptMs_ = millis();
  if (ssid_.isEmpty()) {
    LOG_INFO(kTag, "Enter Cerbo hotspot credentials on the touchscreen");
    return false;
  }
  WiFi.begin(ssid_.c_str(), password_.c_str());
  LOG_INFO(kTag, "Connecting to Cerbo hotspot %s", ssid_.c_str());
  return true;
}

void CerboWifiManager::update() {
  if (restartPending_) {
    restartPending_ = false;
    WiFi.disconnect(false);
    startStation();
  }
  const bool connected = active();
  if (connected != wasConnected_) {
    wasConnected_ = connected;
    if (connected)
      LOG_INFO(kTag, "Connected: IP=%s Cerbo=%s", WiFi.localIP().toString().c_str(),
               WiFi.gatewayIP().toString().c_str());
    else LOG_WARN(kTag, "Wi-Fi disconnected; retrying automatically");
  }
  if (!connected && !ssid_.isEmpty() && millis() - lastAttemptMs_ >= 15000)
    startStation();
}

bool CerboWifiManager::configure(const String& ssid, const String& password) {
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() < 8 ||
      password.length() > 63 || ssid.indexOf('|') >= 0 ||
      password.indexOf('|') >= 0) return false;
  if (!settings_.saveCerboStation(ssid, password)) return false;
  ssid_ = ssid;
  password_ = password;
  restartPending_ = true;
  return true;
}

bool CerboWifiManager::active() const { return WiFi.status() == WL_CONNECTED; }
String CerboWifiManager::ipAddress() const {
  return active() ? WiFi.localIP().toString() : String("Unavailable");
}
