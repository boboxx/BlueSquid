#include "PendingControl.h"
#include "SnapshotRevisionGate.h"
#include <cassert>
#include <initializer_list>
int main() {
  PendingControl<bool> inverter, charger;
  inverter.begin(true, 100);
  charger.begin(true, 200);
  // Slow Cerbo polls must not undo either toggle or a duplicate Home card.
  for (uint32_t now : {200U, 2100U, 4100U}) {
    assert(inverter.display(false, now, 8000));
    assert(inverter.display(false, now, 8000));
    assert(charger.display(false, now, 8000));
  }
  assert(inverter.display(true, 5000, 8000) && !inverter.waiting);
  assert(!inverter.display(false, 6000, 8000)); // Later external change.
  assert(!charger.display(false, 8200, 8000) && !charger.waiting);
  inverter.begin(true, 9000); inverter.begin(false, 9100);
  assert(!inverter.display(true, 9200, 8000)); // Latest tap wins.
  assert(!inverter.display(false, 9300, 8000) && !inverter.waiting);
  PendingControl<uint8_t> brightness;
  brightness.begin(60, UINT32_MAX - 100);
  assert(brightness.display(20, 100, 8000) == 60);
  assert(brightness.display(20, 8000, 8000) == 20);
  // A local request is not a received snapshot. Hold the on display until
  // the Controller accepts it and a snapshot at/after its revision arrives.
  PendingControl<bool> light;
  SnapshotRevisionGate gate;
  light.begin(true, 100);
  assert(light.display(false, 200, 8000) && light.waiting);
  gate.expect(42);
  assert(!gate.accepts(41));
  assert(light.display(false, 300, 8000) && light.waiting);
  assert(gate.accepts(42));
  assert(light.display(true, 400, 8000) && !light.waiting);
  assert(!gate.accepts(41)); // Late stale snapshot after confirmation.
  gate.expect(44); // A newer tap replaces the earlier revision floor.
  assert(!gate.accepts(43) && gate.accepts(45));
  gate.expect(0); // Revision wraparound is valid.
  assert(!gate.accepts(UINT32_MAX) && gate.accepts(0));
}
