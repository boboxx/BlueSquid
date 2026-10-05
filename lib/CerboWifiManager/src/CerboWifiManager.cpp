#include "CerboWifiManager.h"
#include <WiFi.h>
#include "Logging.h"
#include "WifiDefaults.h"
#include "AppConfig.h"
#include "FirmwareUpdate.h"

namespace { constexpr char kTag[] = "CerboWiFi"; }

bool CerboWifiManager::begin() {
  updateRequests_ = xQueueCreate(4, sizeof(UpdateRequest));
  publishUpdateNetwork();
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
  UpdateRequest request;
  while (updateRequests_ && xQueueReceive(updateRequests_, &request, 0) == pdTRUE) {
    if (request.start && updateSession_.begin(request.id, millis())) {
      const auto login = FirmwareUpdate::credentials();
      updateNetworkActive_.store(true);
      updateSsid_ = login.username;
      WiFi.disconnect(false);
      WiFi.begin(login.username, login.password);
      LOG_INFO(kTag, "Joining touchscreen hotspot for firmware update");
    } else if (!request.start && updateSession_.stop(request.id)) restoreStation();
  }
  if (updateSession_.active()) {
    const bool joined = WiFi.status() == WL_CONNECTED && WiFi.SSID() == updateSsid_;
    if (updateSession_.timedOut(millis(), joined)) {
      updateSession_.status.phase = OtaLink::Phase::Failed;
      restoreStation();
      LOG_WARN(kTag, "Firmware update network session expired; returning to Cerbo");
    } else {
      updateSession_.status.phase = joined ? OtaLink::Phase::Ready : OtaLink::Phase::Joining;
      publishUpdateNetwork();
      return;
    }
  }
  publishUpdateNetwork();
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

bool CerboWifiManager::requestUpdateNetwork(bool start, uint32_t id) {
  if (!updateRequests_ || !id) return false;
  UpdateRequest request; request.start = start; request.id = id;
  return xQueueSend(updateRequests_, &request, 0) == pdTRUE;
}
void CerboWifiManager::restoreStation() {
  updateNetworkActive_.store(false);
  WiFi.disconnect(false);
  restartPending_ = false;
  startStation();
}
void CerboWifiManager::publishUpdateNetwork() {
  auto status = updateSession_.status;
  if (status.phase == OtaLink::Phase::Ready) {
    const auto ip = WiFi.localIP();
    for (unsigned i = 0; i < 4; ++i) status.address[i] = ip[i];
  }
  status.version[0] = AppConfig::kFirmwareVersionMajor;
  status.version[1] = AppConfig::kFirmwareVersionMinor;
  status.version[2] = AppConfig::kFirmwareVersionPatch;
  portENTER_CRITICAL(&updateMux_); updateStatus_ = status; portEXIT_CRITICAL(&updateMux_);
}
OtaLink::Status CerboWifiManager::updateNetworkStatus() const {
  portENTER_CRITICAL(&updateMux_); const auto copy = updateStatus_; portEXIT_CRITICAL(&updateMux_);
  return copy;
}
bool CerboWifiManager::active() const {
  return !updateNetworkActive_.load() && WiFi.status() == WL_CONNECTED;
}
String CerboWifiManager::ipAddress() const {
  return active() ? WiFi.localIP().toString() : String("Unavailable");
}
