#include "BleBondGuard.h"

#include <string.h>

#include "Logging.h"

namespace {
constexpr char kTag[] = "BLE";

bool sameKeys(const ble_store_value_sec& left, const ble_store_value_sec& right) {
  return left.ltk_present == right.ltk_present &&
         memcmp(left.ltk, right.ltk, sizeof(left.ltk)) == 0 &&
         left.irk_present == right.irk_present &&
         memcmp(left.irk, right.irk, sizeof(left.irk)) == 0 &&
         left.ediv == right.ediv && left.rand_num == right.rand_num;
}

String text(const ble_addr_t& address) {
  return String(NimBLEAddress(address).toString().c_str());
}
}  // namespace

void BleBondGuard::begin() {
  ble_addr_t addresses[kMaximumBonds]{};
  int count = 0;
  if (ble_store_util_bonded_peers(addresses, &count, kMaximumBonds) != 0) count = 0;
  for (int index = 0; index < count; ++index) {
    Bond bond;
    if (!read(addresses[index], bond)) continue;
    trust(bond);
    LOG_INFO(kTag, "Trusted bond %d: %s", index + 1, text(addresses[index]).c_str());
  }
  if (count == 0) {
    LOG_INFO(kTag, "No paired touchscreen; accepting the first one");
    openWindow(millis(), kFirstPairingWindowMs);
  } else {
    LOG_INFO(kTag, "Pair another touchscreen from System Configuration, "
                   "or hold the lights button 5 s");
  }
}

void BleBondGuard::openWindow(uint32_t now, uint32_t lengthMs) {
  windowActive_ = true;
  windowOpenedMs_ = now;
  windowLengthMs_ = lengthMs;
  LOG_INFO(kTag, "Bluetooth pairing open for %lu s",
           static_cast<unsigned long>(lengthMs / 1000));
}

bool BleBondGuard::windowOpen(uint32_t now) const {
  return windowActive_ && now - windowOpenedMs_ < windowLengthMs_;
}

bool BleBondGuard::verify(const ble_addr_t& peer, uint32_t now) {
  Bond current;
  const bool bonded = read(peer, current);
  if (bonded) {
    const Bond* known = find(peer);
    if (known && same(*known, current)) return true;
  }
  if (windowOpen(now)) {
    if (bonded) {
      trust(current);
      // One new device per window.
      windowActive_ = false;
      LOG_INFO(kTag, "Accepted new Bluetooth device %s; pairing closed", text(peer).c_str());
    }
    return true;
  }
  LOG_WARN(kTag, "Rejected Bluetooth pairing from %s outside pairing window",
           text(peer).c_str());
  restoreStore();
  return false;
}

bool BleBondGuard::read(const ble_addr_t& address, Bond& bond) {
  ble_store_key_sec key{};
  key.peer_addr = address;
  bond = Bond{};
  bond.address = address;
  bond.hasOur = ble_store_read_our_sec(&key, &bond.our) == 0;
  bond.hasPeer = ble_store_read_peer_sec(&key, &bond.peer) == 0;
  bond.used = bond.hasOur || bond.hasPeer;
  return bond.used;
}

bool BleBondGuard::same(const Bond& left, const Bond& right) {
  return left.hasOur == right.hasOur && left.hasPeer == right.hasPeer &&
         (!left.hasOur || sameKeys(left.our, right.our)) &&
         (!left.hasPeer || sameKeys(left.peer, right.peer));
}

BleBondGuard::Bond* BleBondGuard::find(const ble_addr_t& address) {
  for (Bond& bond : trusted_)
    if (bond.used && ble_addr_cmp(&bond.address, &address) == 0) return &bond;
  return nullptr;
}

void BleBondGuard::trust(const Bond& bond) {
  Bond* slot = find(bond.address);
  for (Bond& candidate : trusted_) {
    if (slot) break;
    if (!candidate.used) slot = &candidate;
  }
  if (!slot) {
    // Every slot is taken; replace a trusted bond that NimBLE already evicted.
    for (Bond& candidate : trusted_) {
      Bond stored;
      if (!read(candidate.address, stored)) { slot = &candidate; break; }
    }
  }
  if (!slot) slot = &trusted_[0];
  *slot = bond;
}

void BleBondGuard::restoreStore() {
  ble_addr_t addresses[kMaximumBonds]{};
  int count = 0;
  if (ble_store_util_bonded_peers(addresses, &count, kMaximumBonds) != 0) count = 0;
  for (int index = 0; index < count; ++index) {
    Bond stored;
    const Bond* known = find(addresses[index]);
    if (!known || !read(addresses[index], stored) || !same(*known, stored))
      ble_store_util_delete_peer(&addresses[index]);
  }
  for (const Bond& bond : trusted_) {
    if (!bond.used) continue;
    Bond stored;
    if (read(bond.address, stored) && same(bond, stored)) continue;
    if (bond.hasOur) ble_store_write_our_sec(&bond.our);
    if (bond.hasPeer) ble_store_write_peer_sec(&bond.peer);
  }
}
