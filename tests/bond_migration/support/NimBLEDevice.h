#pragma once
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <string>
#define MYNEWT_VAL(name) 6
using esp_err_t = int;
using nvs_handle_t = int;
constexpr int ESP_OK = 0, ESP_ERR_NVS_NOT_FOUND = 1, NVS_READONLY = 0;
namespace Fixture {
inline bool marker, namespaceExists, migrationOk = true, writeOk = true;
inline int migrations, restarts;
inline std::map<std::string, size_t> records;
inline void reset() {
 marker = namespaceExists = false; migrationOk = writeOk = true;
 migrations = restarts = 0; records.clear();
}
}
inline int nvs_open(const char*, int, nvs_handle_t* handle) {
 *handle = 1; return Fixture::namespaceExists ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
inline int nvs_get_blob(nvs_handle_t, const char* key, void*, size_t* size) {
 auto it = Fixture::records.find(key);
 if (it == Fixture::records.end()) return ESP_ERR_NVS_NOT_FOUND;
 *size = it->second; return ESP_OK;
}
inline void nvs_close(nvs_handle_t) {}
inline struct { void println(const char*) {} void flush() {} } Serial;
inline struct { void restart() { ++Fixture::restarts; } } ESP;
