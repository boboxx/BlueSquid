#include "PendingControl.h"
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
}
