#pragma once
#include "NimBLEDevice.h"
namespace NimBLEBondMigration {
namespace detail {
struct BleStoreValueSecV1 { char data[80]; };
struct BleStoreValueSecCurrent { char data[88]; };
}
inline bool migrateBondStoreToCurrent() {
 ++Fixture::migrations;
 return Fixture::migrationOk;
}
}
