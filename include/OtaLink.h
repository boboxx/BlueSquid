#pragma once
#include <stddef.h>
#include <stdint.h>
#include "OtaCredentials.h"

namespace OtaLink {
constexpr char kUuid[] = "7D8B1006-8A75-4E41-9A6A-35D0A7A18B01";
constexpr uint32_t kLeaseMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kJoinTimeoutMs = 30000;
constexpr size_t kStatusSize = 13;
enum class Phase : uint8_t { Idle, Joining, Ready, Failed };
struct Status {
  Phase phase = Phase::Idle;
  uint32_t id = 0;
  uint8_t address[4]{};
  uint8_t version[3]{};
};
struct View {
  bool supported = false, connected = false, ready = false;
  Status status;
};
inline void encode(uint8_t (&bytes)[kStatusSize], const Status& status) {
  bytes[0] = 1; bytes[1] = static_cast<uint8_t>(status.phase);
  OtaCredentials::writeId(bytes + 2, status.id);
  for (unsigned i = 0; i < 4; ++i) bytes[6 + i] = status.address[i];
  for (unsigned i = 0; i < 3; ++i) bytes[10 + i] = status.version[i];
}
inline bool decode(const uint8_t* bytes, size_t size, Status& status) {
  if (!bytes || size != kStatusSize || bytes[0] != 1 || bytes[1] > 3) return false;
  status.phase = static_cast<Phase>(bytes[1]);
  status.id = OtaCredentials::readId(bytes + 2);
  for (unsigned i = 0; i < 4; ++i) status.address[i] = bytes[6 + i];
  for (unsigned i = 0; i < 3; ++i) status.version[i] = bytes[10 + i];
  return true;
}
// Duplicate requests never extend a lease; stale cancellation cannot end a new one.
struct Session {
  Status status;
  uint32_t started = 0;
  bool active() const { return status.phase == Phase::Joining || status.phase == Phase::Ready; }
  bool begin(uint32_t id, uint32_t now) {
    if (!id || id == status.id) return false;
    status.id = id; status.phase = Phase::Joining; started = now;
    return true;
  }
  bool stop(uint32_t id) {
    if (id != status.id || !active()) return false;
    status.phase = Phase::Idle; return true;
  }
  bool timedOut(uint32_t now, bool connected) const {
    return active() && (uint32_t(now - started) >= kLeaseMs ||
        (!connected && uint32_t(now - started) >= kJoinTimeoutMs));
  }
};
}
