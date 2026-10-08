#include <cassert>
#include "BleBondMigration.h"
int main() {
 using namespace Fixture;
 reset();
 assert(prepareBleBondStorage() && marker && migrations == 0 && restarts == 0);
 reset(); namespaceExists = true; records["our_sec_1"] = 80; records["peer_sec_6"] = 80;
 assert(!prepareBleBondStorage() && marker && migrations == 1 && restarts == 1);
 assert(prepareBleBondStorage() && migrations == 1 && restarts == 1);
 reset(); namespaceExists = true; records["our_sec_1"] = 88;
 assert(prepareBleBondStorage() && migrations == 0 && restarts == 0);
 reset(); namespaceExists = true; records["our_sec_1"] = 80; records["peer_sec_6"] = 79;
 assert(!prepareBleBondStorage() && !marker && migrations == 0);
 reset(); namespaceExists = true; records["peer_sec_1"] = 80; migrationOk = false;
 assert(!prepareBleBondStorage() && !marker && restarts == 0);
 reset(); writeOk = false;
 assert(!prepareBleBondStorage() && !marker && restarts == 0);
 puts("BLE bond migration guard tests passed");
}
