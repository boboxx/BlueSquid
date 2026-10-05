#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace OtaPackage {
constexpr size_t kHeaderSize = 64;
constexpr uint8_t kController = 1;
constexpr uint8_t kTouchscreen = 2;
struct Metadata {
  uint32_t size = 0;
  uint8_t sha256[32]{};
};
inline bool decode(const uint8_t* bytes, size_t length, uint8_t target,
                   size_t capacity, Metadata& result) {
  if (!bytes || length != kHeaderSize ||
      memcmp(bytes, "BSQOTA1\0", 8) || bytes[8] != target ||
      bytes[9] || bytes[10] || bytes[11]) return false;
  const uint32_t size = uint32_t(bytes[12]) | (uint32_t(bytes[13]) << 8) |
      (uint32_t(bytes[14]) << 16) | (uint32_t(bytes[15]) << 24);
  if (size < 32 || size > capacity) return false;
  result.size = size;
  memcpy(result.sha256, bytes + 16, sizeof(result.sha256));
  return true;
}

// Backend owns flash and checksum operations. Activation is deliberately separate
// from feed(): the HTTP layer must confirm that the whole request completed.
template <typename Backend> class Receiver {
 public:
  Receiver(Backend& backend, uint8_t target, size_t capacity)
      : backend_(backend), target_(target), capacity_(capacity) {}
  void reset() {
    if (writing_) backend_.abort();
    writing_ = false;
    headerBytes_ = imageBytes_ = 0;
    error_ = nullptr;
  }
  void fail(const char* message) {
    if (!error_) error_ = message;
    if (writing_) backend_.abort();
    writing_ = false;
  }
  void feed(uint8_t* data, size_t size) {
    if (error_) return;
    if (headerBytes_ < kHeaderSize) {
      const size_t count = size < kHeaderSize - headerBytes_ ? size : kHeaderSize - headerBytes_;
      memcpy(header_ + headerBytes_, data, count);
      headerBytes_ += count; data += count; size -= count;
      if (headerBytes_ != kHeaderSize) return;
      if (!decode(header_, kHeaderSize, target_, capacity_, metadata_)) {
        fail("Invalid package, wrong device, or firmware too large."); return;
      }
      if (!backend_.begin(metadata_.size)) {
        backend_.abort(); fail("Could not start firmware update."); return;
      }
      writing_ = true;
    }
    if (size > metadata_.size - imageBytes_) { fail("Package has extra data."); return; }
    if (size && !backend_.write(data, size)) { fail("Firmware write failed."); return; }
    imageBytes_ += size;
  }
  bool finish() {
    if (error_) return false;
    if (!writing_ || headerBytes_ != kHeaderSize || imageBytes_ != metadata_.size) {
      fail("Incomplete firmware package. Retry the upload."); return false;
    }
    if (!backend_.verify(metadata_.sha256)) { fail("Firmware checksum mismatch."); return false; }
    if (!backend_.activate()) { fail("Firmware image validation failed."); return false; }
    writing_ = false;
    return true;
  }
  const char* error() const { return error_ ? error_ : ""; }
 private:
  Backend& backend_;
  const uint8_t target_;
  const size_t capacity_;
  uint8_t header_[kHeaderSize]{};
  Metadata metadata_{};
  size_t headerBytes_ = 0, imageBytes_ = 0;
  bool writing_ = false;
  const char* error_ = nullptr;
};
}
