#include "OtaCredentials.h"
#include <assert.h>
#include <string>

int main() {
  OtaCredentials::Value value;
  strcpy(value.username, "BlueSquid | van & family");
  strcpy(value.password, "a|b:c&d=123456");
  assert(OtaCredentials::valid(value));
  uint8_t bytes[OtaCredentials::kWireSize]{};
  OtaCredentials::encode(bytes, value, 0x12345678);
  OtaCredentials::Value decoded;
  uint32_t id = 0;
  assert(OtaCredentials::decode(bytes, sizeof(bytes), decoded, id));
  assert(id == 0x12345678 && OtaCredentials::same(value, decoded));
  for (size_t size = 0; size < sizeof(bytes); ++size)
    assert(!OtaCredentials::decode(bytes, size, decoded, id));
  assert(!OtaCredentials::decode(nullptr, sizeof(bytes), decoded, id));
  bytes[0] = 2;
  assert(!OtaCredentials::decode(bytes, sizeof(bytes), decoded, id));
  OtaCredentials::encode(bytes, value, 0);
  assert(!OtaCredentials::decode(bytes, sizeof(bytes), decoded, id));
  for (size_t userLength : {size_t(1), size_t(32)}) {
    for (size_t passwordLength : {size_t(8), size_t(63)}) {
      value = {};
      memset(value.username, 'u', userLength);
      memset(value.password, 'p', passwordLength);
      OtaCredentials::encode(bytes, value, 1);
      assert(OtaCredentials::decode(bytes, sizeof(bytes), decoded, id));
      assert(OtaCredentials::same(value, decoded));
    }
  }
  value.username[0] = 0;
  assert(!OtaCredentials::valid(value));
  strcpy(value.username, "BlueSquid");
  strcpy(value.password, "1234567");
  assert(!OtaCredentials::valid(value));
  memset(value.password, 'p', sizeof(value.password));
  assert(!OtaCredentials::valid(value));
  value.password[63] = 0;
  memset(value.username, 'u', sizeof(value.username));
  assert(!OtaCredentials::valid(value));
  OtaCredentials::encode(bytes, value, 1);
  assert(!OtaCredentials::decode(bytes, sizeof(bytes), decoded, id));
  OtaCredentials::Value first, second;
  strcpy(first.username, "BlueSquid"); strcpy(first.password, "firstpassword");
  second = first; strcpy(second.password, "secondpassword");
  OtaCredentials::SyncState sync;
  assert(sync.needsRequest(first));
  sync.begin(first, 1);
  assert(!sync.current(first) && !sync.needsRequest(first));
  assert(!sync.acknowledge(0) && !sync.acknowledge(2));
  assert(sync.acknowledge(1) && sync.current(first));
  sync.begin(second, 2);
  // Revert the hotspot while the preceding change is still in flight.
  assert(!sync.current(first) && sync.needsRequest(first));
  sync.begin(first, 3);
  assert(!sync.acknowledge(2));
  assert(sync.acknowledge(3) && sync.current(first));
  sync.reset(); // Reconnect must synchronize again even if the SSID is unchanged.
  assert(sync.needsRequest(first) && !sync.acknowledge(3));
  uint8_t ack[4];
  OtaCredentials::writeId(ack, 0xfedcba98);
  assert(OtaCredentials::readId(ack) == 0xfedcba98);
}
