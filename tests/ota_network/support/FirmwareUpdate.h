#pragma once
#include "OtaCredentials.h"
namespace FirmwareUpdate {
inline OtaCredentials::Value credentials() {
  OtaCredentials::Value value;
  strcpy(value.username, "BlueSquid"); strcpy(value.password, "hotspotpassword"); return value;
}
}
