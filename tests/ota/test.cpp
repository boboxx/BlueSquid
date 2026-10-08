#include "OtaPackage.h"
#include <assert.h>
#include <fstream>
#include <iterator>
#include <vector>
struct Backend {
  unsigned starts = 0, activations = 0, aborts = 0;
  bool beginOk = true, writeOk = true, checksumOk = true, imageOk = true;
  std::vector<uint8_t> received;
  bool begin(size_t) { ++starts; received.clear(); return beginOk; }
  bool write(uint8_t* bytes, size_t size) {
    received.insert(received.end(), bytes, bytes + size); return writeOk;
  }
  bool verify(const uint8_t*) { return checksumOk; }
  bool activate() { if (!imageOk) return false; ++activations; return true; }
  void abort() { ++aborts; }
};
void testReceiver(std::vector<uint8_t> data) {
  const std::vector<uint8_t> image(data.begin() + 64, data.end());
  // The header and image may arrive in arbitrarily sized HTTP chunks.
  for (size_t chunk : {size_t(1), size_t(7), size_t(63), size_t(64), size_t(65), size_t(1436)}) {
    Backend backend;
    OtaPackage::Receiver<Backend> receiver(backend, 1, 4096);
    for (size_t i = 0; i < data.size(); i += chunk) {
      receiver.feed(data.data() + i, std::min(chunk, data.size() - i));
      assert(backend.activations == 0);
    }
    assert(receiver.finish() && backend.activations == 1);
    assert(backend.received == image);
  }
  // Every truncated transfer must leave the boot selection untouched.
  for (size_t length : {size_t(0), size_t(12), size_t(63), size_t(64), data.size() - 1}) {
    Backend backend;
    OtaPackage::Receiver<Backend> receiver(backend, 1, 4096);
    receiver.feed(data.data(), length);
    assert(!receiver.finish() && backend.activations == 0);
    receiver.reset();
    receiver.feed(data.data(), data.size());
    assert(receiver.finish()); // A failed upload does not poison the next attempt.
  }
  for (unsigned failure = 0; failure < 8; ++failure) {
    Backend backend;
    backend.beginOk = failure != 0;
    backend.writeOk = failure != 1;
    backend.checksumOk = failure != 2;
    backend.imageOk = failure != 3;
    OtaPackage::Receiver<Backend> receiver(backend, failure == 4 ? 2 : 1,
                                         failure == 5 ? 10 : 4096);
    receiver.feed(data.data(), data.size());
    if (failure == 6) receiver.feed(data.data(), 1); // Extra bytes.
    if (failure == 7) receiver.fail("HTTP request failed");
    assert(!receiver.finish() && backend.activations == 0);
    if (failure == 4 || failure == 5) assert(backend.starts == 0);
  }
  Backend backend;
  OtaPackage::Receiver<Backend> receiver(backend, 1, 4096);
  receiver.fail("Unauthorized request");
  receiver.feed(data.data(), data.size());
  assert(!receiver.finish() && backend.starts == 0);
  receiver.reset();
  receiver.feed(data.data(), 100);
  receiver.reset(); // Disconnected upload aborts the flash writer.
  assert(backend.aborts == 1 && backend.activations == 0);
}
int main(int argc, char** argv) {
  assert(argc == 2);
  std::ifstream stream(argv[1], std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)), {});
  assert(data.size() > OtaPackage::kHeaderSize);
  testReceiver(data);
  OtaPackage::Metadata metadata;
  assert(OtaPackage::decode(data.data(), 64, OtaPackage::kController, 4096, metadata));
  assert(metadata.size == data.size() - 64);
  assert(!OtaPackage::decode(data.data(), 64, OtaPackage::kTouchscreen, 4096, metadata));
  assert(!OtaPackage::decode(data.data(), 64, OtaPackage::kTouchscreen10, 4096, metadata));
  assert(!OtaPackage::decode(data.data(), 64, OtaPackage::kController, metadata.size - 1, metadata));
  for (size_t size = 0; size < 64; ++size)
    assert(!OtaPackage::decode(data.data(), size, 1, 4096, metadata));
  assert(!OtaPackage::decode(nullptr, 64, 1, 4096, metadata));
  for (unsigned i = 0; i < 12; ++i) {
    data[i] ^= 0x80;
    assert(!OtaPackage::decode(data.data(), 64, 1, 4096, metadata));
    data[i] ^= 0x80;
  }
  for (unsigned i = 12; i < 16; ++i) data[i] = 0;
  assert(!OtaPackage::decode(data.data(), 64, 1, 4096, metadata));
  data[12] = 31;
  assert(!OtaPackage::decode(data.data(), 64, 1, 4096, metadata));
  for (unsigned i = 12; i < 16; ++i) data[i] = 255;
  assert(!OtaPackage::decode(data.data(), 64, 1, 4096, metadata));
}
