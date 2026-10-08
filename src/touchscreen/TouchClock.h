#pragma once
#include <stddef.h>
#include <stdint.h>
namespace TouchClock {
struct Reading { bool valid; unsigned minute; char text[6]; };
void begin();
Reading read();
// "YYYY-MM-DD HH:MM:SS" after a phone sync, "HH:MM:SS" for a manually set
// time without a date, or an empty string when the clock is not set.
void timestamp(char* out, size_t size);
bool sync(int64_t epoch);
bool setTime(unsigned hour, unsigned minute);
void setZone(unsigned zone);
unsigned zone();
constexpr const char* kZones = "Atlantic\nEastern\nCentral\nMountain\nPacific\nNewfoundland\nUTC";
}
