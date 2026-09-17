#include "RvcFanManager.h"
#include "AppConfig.h"
#include "Logging.h"
#include <esp_system.h>

bool RvcFanManager::begin() {
  if (initialized_) { twai_stop(); twai_driver_uninstall(); }
  initialized_ = seen_ = pending_ = sent_ = false; phase_ = error_ = 0; status_ = {};
  configRevision_ = settings_.rvcFanRevision();
  config_ = settings_.loadRvcFanConfiguration();
  outputs_.setFanCommandHandler([this](uint8_t speed) { return setSpeed(speed); });
  outputs_.setFanDirectionHandler([this](bool intake) { return setDirection(intake); });
  source_ = config_.source;
  publish();
  if (!config_.enabled) return true;
  twai_general_config_t general = TWAI_GENERAL_CONFIG_DEFAULT(static_cast<gpio_num_t>(AppConfig::Can::kTxPin),
      static_cast<gpio_num_t>(AppConfig::Can::kRxPin), TWAI_MODE_NORMAL);
  general.tx_queue_len = 0; // Never retain commands to replay after reconnection.
  general.rx_queue_len = 32;
  const twai_timing_config_t timing = TWAI_TIMING_CONFIG_250KBITS();
  const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();
  if (twai_driver_install(&general, &timing, &filter) != ESP_OK) { error_ = 1; publish(); return false; }
  if (twai_start() != ESP_OK) { twai_driver_uninstall(); error_ = 1; publish(); return false; }
  initialized_ = true;
  // Development identity: unique serial from eFuse; manufacturer code 0.
  // A distributed product must use its own assigned manufacturer code.
  const uint64_t serial = ESP.getEfuseMac() & 0x1FFFFF;
  for (unsigned i = 0; i < 8; ++i) name_[i] = serial >> (i * 8);
  name_[7] = 0x80;
  probe();
  LOG_INFO("RV-C", "FA75 roof fan instance %u, CAN TX=%d RX=%d, 250 kbit/s", config_.instance,
           AppConfig::Can::kTxPin, AppConfig::Can::kRxPin);
  return true;
}
bool RvcFanManager::send(uint32_t dgn, const uint8_t (&data)[8], uint8_t destination, bool probeFrame) {
  if (!initialized_) return false;
  twai_message_t msg{};
  msg.extd = 1; msg.ss = 1; // Single-shot: a disconnected bus must not retry stale commands.
  msg.identifier = RvcFan::id(dgn, probeFrame ? 254 : source_, destination);
  msg.data_length_code = 8;
  memcpy(msg.data, data, 8);
  return twai_transmit(&msg, 0) == ESP_OK;
}
void RvcFanManager::probe() {
  uint8_t data[8]; RvcFan::request(RvcFan::kClaim, data);
  phase_ = 0; phaseMs_ = millis();
  if (send(RvcFan::kRequest, data, source_, true)) phase_ = 1;
}
void RvcFanManager::conflict() {
  pending_ = sent_ = false;
  if (source_ <= 151) { phase_ = 4; error_ = 2; return; }
  --source_; probe();
}
bool RvcFanManager::setSpeed(uint8_t speed) {
  if (speed > 100 || !config_.enabled || phase_ != 3 || !seen_ ||
      millis() - lastSeenMs_ > RvcFan::kTimeoutMs || error_ == 1) return false;
  speed = speed ? min(100, max(10, ((speed + 5) / 10) * 10)) : 0;
  if (!pending_) directionPending_ = false;
  speedPending_ = true;
  desired_ = speed; pending_ = true; sent_ = false; error_ = 0; queuedMs_ = millis();
  publish(); return true;
}
bool RvcFanManager::setDirection(bool intake) {
  if (!config_.enabled || phase_ != 3 || !seen_ || status_.direction > 1 ||
      millis() - lastSeenMs_ > RvcFan::kTimeoutMs || error_ == 1) return false;
  if (!pending_) speedPending_ = false;
  directionPending_ = true; desiredIntake_ = intake;
  pending_ = true; sent_ = false; error_ = 0; queuedMs_ = millis();
  publish(); return true;
}
void RvcFanManager::publish() {
  const bool online = initialized_ && phase_ == 3 && seen_ &&
      millis() - lastSeenMs_ <= RvcFan::kTimeoutMs && error_ != 1;
  outputs_.setRvcFanStatus(config_.enabled, online, status_.on, status_.speed,
                          pending_, source_, config_.instance, error_, status_.direction);
}
void RvcFanManager::diagnostic() {
  // This node is a control panel (DSA 68), not the fan itself.
  uint8_t data[8] = {0x05, 68, 255, 255, 255, 255, 255, 255};
  if (send(0x1FECA, data)) diagnosticMs_ = millis();
}
void RvcFanManager::update() {
  if (configRevision_ != settings_.rvcFanRevision()) begin();
  if (!initialized_) return;
  const uint32_t now = millis();
  twai_status_info_t bus{};
  if (twai_get_status_info(&bus) != ESP_OK) return;
  if (bus.state == TWAI_STATE_BUS_OFF) {
    pending_ = sent_ = seen_ = false; phase_ = 0; error_ = 1;
    twai_initiate_recovery(); publish(); return;
  }
  if (bus.state == TWAI_STATE_RECOVERING) { publish(); return; }
  if (bus.state == TWAI_STATE_STOPPED) {
    if (twai_start() == ESP_OK) { error_ = 0; probe(); }
    publish(); return;
  }
  twai_message_t msg{};
  for (unsigned budget = 0; budget < 64 && twai_receive(&msg, 0) == ESP_OK; ++budget) {
    if (!msg.extd || msg.rtr || msg.data_length_code != 8) continue;
    const uint32_t dgn = RvcFan::dgn(msg.identifier);
    if ((msg.identifier & 255) == source_) { conflict(); continue; }
    if (dgn == RvcFan::kRequest && phase_ == 3 &&
        (((msg.identifier >> 8) & 255) == 255 || ((msg.identifier >> 8) & 255) == source_)) {
      const uint32_t requested = msg.data[0] | (uint32_t(msg.data[1]) << 8) | (uint32_t(msg.data[2]) << 16);
      if (requested == RvcFan::kClaim) send(RvcFan::kClaim, name_, 0);
      else if (requested == 0x1FECA) diagnostic();
      else if (requested == 0xFEEB) {
        const uint8_t product[8] = {'B','S','*','F','*','*',255,255};
        send(0xFEEB, product);
      }
    }
    RvcFan::Status reported;
    if (RvcFan::decode(msg.identifier, msg.extd, msg.rtr, msg.data, msg.data_length_code, config_.instance, reported)) {
      status_ = reported; lastSeenMs_ = now; seen_ = true;
      if (pending_ && sent_ &&
          (!speedPending_ || (reported.on == (desired_ != 0) && (!desired_ || reported.speed == desired_))) &&
          (!directionPending_ || reported.direction == (desiredIntake_ ? 1 : 0))) { pending_ = sent_ = false; error_ = 0; }
      LOG_DEBUG("RV-C", "Fan instance %u on=%u speed=%u", config_.instance, reported.on, reported.speed);
    }
  }
  if (phase_ == 0 && now - phaseMs_ >= 1000) probe();
  if (phase_ == 1 && now - phaseMs_ >= 300 && send(RvcFan::kClaim, name_, 0)) { phase_ = 2; phaseMs_ = now; }
  if (phase_ == 2 && now - phaseMs_ >= 300) { phase_ = 3; pollMs_ = now - 5000; }
  if (phase_ == 3 && now - pollMs_ >= 5000) {
    uint8_t data[8]; RvcFan::request(RvcFan::kStatus, data); data[3] = config_.instance;
    if (send(RvcFan::kRequest, data)) pollMs_ = now;
  }
  if (phase_ == 3 && now - diagnosticMs_ >= 5000) diagnostic();
  if (pending_ && (!seen_ || now - lastSeenMs_ > RvcFan::kTimeoutMs)) { pending_ = sent_ = false; error_ = 3; }
  if (pending_ && !sent_ && now - queuedMs_ >= 150) {
    uint8_t data[8];
    if (speedPending_) RvcFan::command(config_.instance, desired_, data);
    else RvcFan::directionCommand(config_.instance, desiredIntake_, data);
    if (directionPending_) data[3] = desiredIntake_ ? 0xFD : 0xFC;
    if (send(RvcFan::kCommand, data)) {
      sent_ = true; sentMs_ = now;
      LOG_INFO("RV-C", "Fan instance %u command speed=%d direction=%d", config_.instance,
               speedPending_ ? desired_ : -1, directionPending_ ? (desiredIntake_ ? 1 : 0) : -1);
    }
  }
  if (pending_ && ((!sent_ && now - queuedMs_ >= 2000) || (sent_ && now - sentMs_ >= 6000))) {
    pending_ = sent_ = false; error_ = 3;
    LOG_WARN("RV-C", "Fan command was not confirmed");
  }
  publish();
}
