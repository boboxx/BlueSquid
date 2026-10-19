#if defined(BLUESQUID_TOUCHSCREEN_10IN) && BLUESQUID_TOUCHSCREEN_10IN
#include "TouchBleCrypto.h"

#include <Arduino.h>
#include <cstring>
#include "esp_random.h"
#include "host/ble_hs.h"
#include "mbedtls/ecdh.h"

// The pinned P4 TinyCrypt backend computes an incorrect Secure Connections
// DHKey (esp-hosted-mcu#241 / ESP-IDF 9fd7cb7e606). Route only this operation
// through mbedTLS until the prebuilt framework includes the upstream fix.
// NimBLE's inputs and output are all little-endian P-256 integers.
namespace {
int randomBytes(void*, unsigned char* output, size_t length) {
  esp_fill_random(output, length);
  return 0;
}
}

extern "C" int __wrap_ble_sm_alg_gen_dhkey(const uint8_t* x, const uint8_t* y,
                                          const uint8_t* privateKey, uint8_t* output) {
  mbedtls_ecp_group group;
  mbedtls_ecp_point peer;
  mbedtls_mpi secret, key;
  mbedtls_ecp_group_init(&group);
  mbedtls_ecp_point_init(&peer);
  mbedtls_mpi_init(&secret);
  mbedtls_mpi_init(&key);
  memset(output, 0, 32);
  int rc = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
  if (!rc) rc = mbedtls_mpi_read_binary_le(&peer.MBEDTLS_PRIVATE(X), x, 32);
  if (!rc) rc = mbedtls_mpi_read_binary_le(&peer.MBEDTLS_PRIVATE(Y), y, 32);
  if (!rc) rc = mbedtls_mpi_lset(&peer.MBEDTLS_PRIVATE(Z), 1);
  if (!rc) rc = mbedtls_ecp_check_pubkey(&group, &peer);
  if (!rc) rc = mbedtls_mpi_read_binary_le(&key, privateKey, 32);
  if (!rc) rc = mbedtls_ecp_check_privkey(&group, &key);
  if (!rc) rc = mbedtls_ecdh_compute_shared(&group, &secret, &peer, &key, randomBytes, nullptr);
  if (!rc) rc = mbedtls_mpi_write_binary_le(&secret, output, 32);
  mbedtls_mpi_free(&key);
  mbedtls_mpi_free(&secret);
  mbedtls_ecp_point_free(&peer);
  mbedtls_ecp_group_free(&group);
  return rc == 0 ? 0 : BLE_HS_EUNKNOWN;
}

extern "C" int ble_sm_alg_gen_dhkey(const uint8_t*, const uint8_t*, const uint8_t*, uint8_t*);

bool touchBleCryptoSelfTest() {
  // Bluetooth Core Specification LE Secure Connections P-256 sample, big endian.
  static const uint8_t privateKey[32] = {
    0x3f,0x49,0xf6,0xd4,0xa3,0xc5,0x5f,0x38,0x74,0xc9,0xb3,0xe3,0xd2,0x10,0x3f,0x50,
    0x4a,0xff,0x60,0x7b,0xeb,0x40,0xb7,0x99,0x58,0x99,0xb8,0xa6,0xcd,0x3c,0x1a,0xbd};
  static const uint8_t publicKey[64] = {
    0x1e,0xa1,0xf0,0xf0,0x1f,0xaf,0x1d,0x96,0x09,0x59,0x22,0x84,0xf1,0x9e,0x4c,0x00,
    0x47,0xb5,0x8a,0xfd,0x86,0x15,0xa6,0x9f,0x55,0x90,0x77,0xb2,0x2f,0xaa,0xa1,0x90,
    0x4c,0x55,0xf3,0x3e,0x42,0x9d,0xad,0x37,0x73,0x56,0x70,0x3a,0x9a,0xb8,0x51,0x60,
    0x47,0x2d,0x11,0x30,0xe2,0x8e,0x36,0x76,0x5f,0x89,0xaf,0xf9,0x15,0xb1,0x21,0x4a};
  static const uint8_t expected[32] = {
    0xec,0x02,0x34,0xa3,0x57,0xc8,0xad,0x05,0x34,0x10,0x10,0xa6,0x0a,0x39,0x7d,0x9b,
    0x99,0x79,0x6b,0x13,0xb4,0xf8,0x66,0xf1,0x86,0x8d,0x34,0xf3,0x73,0xbf,0xa6,0x98};
  uint8_t x[32], y[32], key[32], output[32], zero[32]{};
  for (size_t i = 0; i < 32; ++i) {
    x[i] = publicKey[31-i]; y[i] = publicKey[63-i]; key[i] = privateKey[31-i];
  }
  bool ok = ble_sm_alg_gen_dhkey(x, y, key, output) == 0;
  for (size_t i = 0; i < 32; ++i) ok = ok && output[i] == expected[31-i];
  ok = ok && ble_sm_alg_gen_dhkey(zero, zero, key, output) != 0;
  ok = ok && ble_sm_alg_gen_dhkey(x, y, zero, output) != 0;
  Serial.printf("BLE P-256 self-test: %s\n", ok ? "passed" : "FAILED");
  return ok;
}
#endif
