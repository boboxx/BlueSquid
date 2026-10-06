#pragma once
#include <stdint.h>
namespace TouchClock {
struct Reading { bool valid; unsigned minute; char text[6]; };
void begin();
Reading read();
bool sync(int64_t epoch);
bool setTime(unsigned hour, unsigned minute);
void setZone(unsigned zone);
unsigned zone();
constexpr const char* kZones = "Atlantic\nEastern\nCentral\nMountain\nPacific\nNewfoundland\nUTC";
}
