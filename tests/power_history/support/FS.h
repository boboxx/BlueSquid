#pragma once
// In-memory stand-in for the Arduino FS API used by TouchHistory.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#define FILE_READ "r"
#define FILE_APPEND "a"

class String : public std::string {
 public:
  String() = default;
  String(const char* text) : std::string(text) {}
  String(const std::string& text) : std::string(text) {}
  bool isEmpty() const { return empty(); }
};

namespace fs {

struct Storage {
  std::map<std::string, std::vector<uint8_t>> files;
  std::map<std::string, bool> directories;
};

class File {
 public:
  File() = default;
  File(Storage* storage, std::string path, bool directory, bool append)
      : storage_(storage), path_(std::move(path)), directory_(directory),
        append_(append) {}
  explicit operator bool() const { return storage_ != nullptr; }
  bool isDirectory() const { return directory_; }
  const char* path() const { return path_.c_str(); }
  size_t size() const { return storage_->files[path_].size(); }
  size_t write(const uint8_t* data, size_t length) {
    if (!append_) return 0;
    auto& bytes = storage_->files[path_];
    bytes.insert(bytes.end(), data, data + length);
    return length;
  }
  size_t read(uint8_t* data, size_t length) {
    const auto& bytes = storage_->files[path_];
    const size_t count = std::min(length, bytes.size() - position_);
    memcpy(data, bytes.data() + position_, count);
    position_ += count;
    return count;
  }
  File openNextFile() {
    if (!directory_) return File();
    const std::string prefix = path_ + "/";
    size_t index = 0;
    for (const auto& entry : storage_->files) {
      if (entry.first.compare(0, prefix.size(), prefix) != 0) continue;
      if (index++ == position_) {
        ++position_;
        return File(storage_, entry.first, false, false);
      }
    }
    return File();
  }
  void close() {}

 private:
  Storage* storage_ = nullptr;
  std::string path_;
  bool directory_ = false;
  bool append_ = false;
  size_t position_ = 0;
};

class FS {
 public:
  bool exists(const char* path) const {
    return storage.files.count(path) || storage.directories.count(path);
  }
  bool mkdir(const char* path) {
    storage.directories[path] = true;
    return true;
  }
  bool remove(const String& path) { return storage.files.erase(path) == 1; }
  File open(const char* path, const char* mode = FILE_READ) {
    if (storage.directories.count(path))
      return File(&storage, path, true, false);
    const bool append = strcmp(mode, FILE_APPEND) == 0;
    if (!append && !storage.files.count(path)) return File();
    storage.files[path];
    return File(&storage, path, false, append);
  }
  Storage storage;
};

}  // namespace fs

using fs::File;
using fs::FS;
