#pragma once
#include <Arduino.h>
#include "OtaCredentials.h"
#include "OtaLink.h"

namespace FirmwareUpdate {
// Touchscreen supplies its current saved hotspot settings; Controller loads
// the last synchronized pair, falling back to the shared build defaults.
bool begin(const OtaCredentials::Value* hotspot = nullptr);
// Queued: persistence and HTTP credential changes run only in the OTA worker.
bool setHotspotCredentials(const OtaCredentials::Value& value, uint32_t requestId = 0);
uint32_t credentialsAcknowledgement();
OtaCredentials::Value credentials();
bool available();
using ControllerRequest = bool (*)(bool start);
using ControllerStatus = OtaLink::View (*)();
void setControllerRelay(ControllerRequest request, ControllerStatus status);
}
