#include "DisplaySchedule.h"
#include "TouchClock.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
int64_t fakeMicroseconds = 0;
int main() {
  using namespace DisplaySchedule;
  assert(!overnight(false, true, 1380, 1320, 420));
  assert(!overnight(true, false, 1380, 1320, 420));
  assert(!overnight(true, true, 1319, 1320, 420));
  assert(overnight(true, true, 1320, 1320, 420));
  assert(overnight(true, true, 0, 1320, 420));
  assert(overnight(true, true, 419, 1320, 420));
  assert(!overnight(true, true, 420, 1320, 420));
  assert(overnight(true, true, 600, 540, 720));
  assert(!overnight(true, true, 600, 600, 600));
  State s;
  assert(s.update(false, false, false, 60000, 0, 0, 30000) == State::None);
  assert(s.update(false, false, false, 60000, 0, 60000, 30000) == State::Sleep);
  assert(s.update(true, false, false, 60001, 60000, 0, 30000) == State::Sleep);
  assert(s.update(true, true, true, 70000, 70000, 0, 30000) == State::Wake);
  assert(s.update(true, false, false, 99999, 70000, 0, 30000) == State::None);
  assert(s.update(true, false, false, 100000, 70000, 0, 30000) == State::Sleep);
  assert(s.update(false, true, false, 100001, 70000, 0, 30000) == State::Wake);
  s.wasOvernight = true;
  assert(s.update(true, true, true, UINT32_MAX - 1000, 0, 0, 30000) == State::Wake);
  assert(s.update(true, false, false, 28999, 0, 0, 30000) == State::Sleep);
  TouchClock::begin();
  assert(!TouchClock::read().valid);
  assert(!TouchClock::setTime(24, 0));
  assert(TouchClock::setTime(23, 59));
  fakeMicroseconds += 61000000;
  assert(!strcmp(TouchClock::read().text, "00:00"));
  assert(!TouchClock::sync(0));
  // UTC noon Jan 1 and July 1, 2026: Atlantic switches from UTC-4 to UTC-3.
  assert(TouchClock::sync(1767268800));
  assert(!strcmp(TouchClock::read().text, "08:00"));
  assert(TouchClock::sync(1782907200));
  assert(!strcmp(TouchClock::read().text, "09:00"));
  TouchClock::setZone(5);
  assert(!strcmp(TouchClock::read().text, "09:30"));
  TouchClock::setZone(6);
  assert(!strcmp(TouchClock::read().text, "12:00"));
  fakeMicroseconds += 61LL * 1000000;
  assert(!strcmp(TouchClock::read().text, "12:01"));
}
