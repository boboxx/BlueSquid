#pragma once
#include <deque>
#include <vector>
#include <cstring>
struct TestQueue { size_t capacity, size; std::deque<std::vector<unsigned char>> entries; };
using QueueHandle_t = TestQueue*;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t size) { return new TestQueue{capacity, size, {}}; }
inline int xQueueSend(QueueHandle_t queue, const void* value, int) {
  if (queue->entries.size() == queue->capacity) return 0;
  const auto* begin = static_cast<const unsigned char*>(value);
  queue->entries.emplace_back(begin, begin + queue->size); return 1;
}
inline int xQueueReceive(QueueHandle_t queue, void* value, int) {
  if (queue->entries.empty()) return 0;
  memcpy(value, queue->entries.front().data(), queue->size); queue->entries.pop_front(); return 1;
}
