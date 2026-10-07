#include "InverterModeRequest.h"
#include <cassert>
int main() {
  InverterModeRequest request;
  // Both on. Inverter off, then charger off before the next Cerbo poll.
  assert(request.request(true, false, 3, 1000) == 1);
  assert(request.request(false, false, 3, 1500) == 4);  // Stale poll still says 3.
  // A stale poll does not confirm; the matching one does.
  request.observe(3, true);
  assert(request.base(3, 2000) == 4);
  request.observe(4, true);
  assert(request.base(4, 2100) == 4 && request.mode == 0);
  // Without confirmation, the request expires and polled state is used.
  assert(request.request(true, true, 4, 3000) == 2);
  assert(request.base(4, 3000 + InverterModeRequest::kConfirmMs) == 4);
  // A failed write falls back to the reported mode immediately.
  assert(request.request(false, true, 2, 20000) == 3);
  request.failed(3);
  assert(request.base(2, 20001) == 2);
  // Time wraparound.
  assert(request.request(true, false, 3, UINT32_MAX - 5) == 1);
  assert(request.base(3, 10) == 1);
}
