#include "OtaLink.h"
#include <assert.h>

int main() {
  OtaLink::Session session;
  assert(!session.active());
  assert(!session.begin(0, 0));
  assert(session.begin(7, 100));
  assert(session.active() && session.status.phase == OtaLink::Phase::Joining);
  assert(!session.begin(7, 10000) && session.started == 100);
  assert(!session.timedOut(30099, false));
  assert(session.timedOut(30100, false));
  assert(!session.timedOut(30100, true));
  assert(!session.timedOut(OtaLink::kLeaseMs + 99, true));
  assert(session.timedOut(OtaLink::kLeaseMs + 100, true));
  assert(session.begin(8, 200));
  assert(!session.stop(7));
  assert(session.stop(8) && !session.active());
  assert(!session.begin(8, 300)); // A delayed duplicate cannot reopen a stopped lease.
  assert(session.begin(9, UINT32_MAX - 50));
  assert(!session.timedOut(20, false));
  assert(session.timedOut(30000, false));
  session.status.phase = OtaLink::Phase::Ready;
  session.status.address[0] = 192; session.status.address[1] = 168;
  session.status.address[2] = 4; session.status.address[3] = 2;
  session.status.version[0] = 1; session.status.version[2] = 22;
  uint8_t bytes[OtaLink::kStatusSize]{};
  OtaLink::encode(bytes, session.status);
  OtaLink::Status decoded;
  assert(OtaLink::decode(bytes, sizeof(bytes), decoded));
  assert(decoded.phase == OtaLink::Phase::Ready && decoded.id == 9);
  assert(decoded.address[3] == 2 && decoded.version[2] == 22);
  for (size_t size = 0; size < sizeof(bytes); ++size)
    assert(!OtaLink::decode(bytes, size, decoded));
  assert(!OtaLink::decode(nullptr, sizeof(bytes), decoded));
  bytes[0] = 2;
  assert(!OtaLink::decode(bytes, sizeof(bytes), decoded));
  bytes[0] = 1; bytes[1] = 4;
  assert(!OtaLink::decode(bytes, sizeof(bytes), decoded));
}
