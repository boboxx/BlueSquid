#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace OtaCredentials {
constexpr char kUuid[] = "7D8B1005-8A75-4E41-9A6A-35D0A7A18B01";
struct Value {
  char username[33]{};
  char password[64]{};
};
static_assert(sizeof(Value) == 97, "Credential record layout changed");
constexpr size_t kWireSize = 1 + 4 + sizeof(Value);
inline bool valid(const Value& value) {
  const auto* userEnd = static_cast<const char*>(memchr(value.username, 0, sizeof(value.username)));
  const auto* passwordEnd = static_cast<const char*>(memchr(value.password, 0, sizeof(value.password)));
  return userEnd && userEnd != value.username && passwordEnd && passwordEnd - value.password >= 8;
}
inline bool same(const Value& a, const Value& b) {
  return valid(a) && valid(b) && !strcmp(a.username, b.username) && !strcmp(a.password, b.password);
}
inline void writeId(uint8_t* bytes, uint32_t id) {
  for (unsigned i = 0; i < 4; ++i) bytes[i] = static_cast<uint8_t>(id >> (i * 8));
}
inline uint32_t readId(const uint8_t* bytes) {
  return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
      (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
inline void encode(uint8_t (&bytes)[kWireSize], const Value& value, uint32_t id) {
  bytes[0] = 1;
  writeId(bytes + 1, id);
  memcpy(bytes + 5, &value, sizeof(value));
}
inline bool decode(const uint8_t* bytes, size_t size, Value& value, uint32_t& id) {
  if (!bytes || size != kWireSize || bytes[0] != 1) return false;
  Value next;
  memcpy(&next, bytes + 5, sizeof(next));
  const uint32_t nextId = readId(bytes + 1);
  if (!nextId || !valid(next)) return false;
  value = next;
  id = nextId;
  return true;
}
// Tracks the pair confirmed by the Controller, including changes made while a
// previous pair is still awaiting its persistence acknowledgement.
struct SyncState {
  Value requested, confirmed;
  uint32_t id = 0;
  bool pending = false, synced = false;
  void reset() { pending = synced = false; id = 0; }
  bool current(const Value& desired) const { return synced && same(desired, confirmed); }
  bool needsRequest(const Value& desired) const {
    return !current(desired) && (!pending || !same(desired, requested));
  }
  void begin(const Value& desired, uint32_t requestId) {
    requested = desired; id = requestId; pending = true; synced = false;
  }
  bool acknowledge(uint32_t receivedId) {
    if (!pending || !receivedId || receivedId != id) return false;
    confirmed = requested; pending = false; synced = true;
    return true;
  }
};
}
