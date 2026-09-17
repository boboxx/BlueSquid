#include "lvgl_port.h"

#include <Arduino.h>
#include <lvgl.h>
#include <src/draw/lv_draw_buf_private.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <atomic>

using esp_panel::drivers::LCD;
using esp_panel::drivers::Touch;
using esp_panel::drivers::TouchPoint;

static void* lvglMemoryPool = nullptr;
extern "C" void* bluesquid_lvgl_pool_alloc(size_t size) {
  return size == LV_MEM_SIZE ? lvglMemoryPool : nullptr;
}

namespace {
constexpr uint32_t kTickPeriodMs = 2;
constexpr uint32_t kBufferLines = 20;
constexpr uint32_t kTaskMinDelayMs = 2;
constexpr uint32_t kTaskMaxDelayMs = 500;
constexpr uint32_t kTaskStackSize = 8 * 1024;
constexpr UBaseType_t kTaskPriority = 2;

SemaphoreHandle_t lvglMutex = nullptr;
SemaphoreHandle_t touchDetected = nullptr;
TaskHandle_t lvglTaskHandle = nullptr;
esp_timer_handle_t tickTimer = nullptr;
lv_display_t* display = nullptr;
lv_indev_t* pointerDevice = nullptr;
void* drawBuffers[2]{};
std::atomic_bool displaySleeping{false};
std::atomic_bool touchActivityPending{false};
std::atomic_bool wakeRequestPending{false};
std::atomic_bool suppressWakeGesture{false};
std::atomic_uint32_t wakeGestureStartedMs{0};

// Opacity and transforms can require an ARGB layer larger than LVGL's
// fixed widget pool (the 250x250 colour wheel alone is 250,000 bytes).
// These are CPU buffers, so use PSRAM just like the partial display buffers.
void* allocateRenderBuffer(size_t size, lv_color_format_t) {
  void* buffer = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buffer) buffer = heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!buffer) {
    Serial.printf("LVGL render allocation failed: %u bytes; largest PSRAM=%u internal=%u\n",
                  static_cast<unsigned>(size),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)),
                  static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
  }
  return buffer;
}

void freeRenderBuffer(void* buffer) { heap_caps_free(buffer); }

void tickIncrement(void*) {
  lv_tick_inc(kTickPeriodMs);
}

void flushDisplay(lv_display_t* displayDriver, const lv_area_t* area,
                  uint8_t* pixels) {
  auto* lcd = static_cast<LCD*>(lv_display_get_user_data(displayDriver));
  if (lcd == nullptr) {
    lv_display_flush_ready(displayDriver);
    return;
  }
  lcd->drawBitmap(area->x1, area->y1, area->x2 - area->x1 + 1,
                  area->y2 - area->y1 + 1, pixels);
  // The Waveshare 7-inch board uses an RGB panel; drawBitmap copies the
  // rendered rectangle before returning, so LVGL may reuse the partial buffer.
  lv_display_flush_ready(displayDriver);
}

void readTouch(lv_indev_t* indev, lv_indev_data_t* data) {
  auto* touch = static_cast<Touch*>(lv_indev_get_user_data(indev));
  data->state = LV_INDEV_STATE_RELEASED;
  if (touch == nullptr) return;
  if (touch->isInterruptEnabled() &&
      xSemaphoreTake(touchDetected, 0) == pdFALSE) {
    return;
  }
  TouchPoint point{};
  if (touch->readPoints(&point, 1, 0) > 0) {
    touchActivityPending.store(true, std::memory_order_relaxed);
    if (displaySleeping.load(std::memory_order_relaxed)) {
      wakeRequestPending.store(true, std::memory_order_relaxed);
      suppressWakeGesture.store(true, std::memory_order_relaxed);
      wakeGestureStartedMs.store(
          static_cast<uint32_t>(esp_timer_get_time() / 1000ULL),
          std::memory_order_relaxed);
      return;
    }
    // Consume the touch that woke the panel until it is released. The timeout
    // is a fallback for touch controllers that do not signal a release edge.
    if (suppressWakeGesture.load(std::memory_order_relaxed)) {
      const uint32_t nowMs =
          static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
      if (nowMs - wakeGestureStartedMs.load(std::memory_order_relaxed) < 750) {
        return;
      }
      suppressWakeGesture.store(false, std::memory_order_relaxed);
    }
    data->point.x = point.x;
    data->point.y = point.y;
    data->state = LV_INDEV_STATE_PRESSED;
  } else {
    suppressWakeGesture.store(false, std::memory_order_relaxed);
  }
}

bool onTouchInterrupt(void*) {
  BaseType_t taskWoken = pdFALSE;
  xSemaphoreGiveFromISR(touchDetected, &taskWoken);
  if (taskWoken == pdTRUE) portYIELD_FROM_ISR();
  return false;
}

void lvglTask(void*) {
  uint32_t lastMemoryReportMs = 0;
  while (true) {
    uint32_t delayMs = kTaskMaxDelayMs;
    if (lvgl_port_lock(-1)) {
      delayMs = lv_timer_handler();
      if (millis() - lastMemoryReportMs >= 30000) {
        lastMemoryReportMs = millis();
        lv_mem_monitor_t memory{};
        lv_mem_monitor(&memory);
        Serial.printf("LVGL pool: used=%u%% free=%u largest=%u bytes\n",
                      memory.used_pct, static_cast<unsigned>(memory.free_size),
                      static_cast<unsigned>(memory.free_biggest_size));
      }
      lvgl_port_unlock();
    }
    if (delayMs < kTaskMinDelayMs) delayMs = kTaskMinDelayMs;
    if (delayMs > kTaskMaxDelayMs) delayMs = kTaskMaxDelayMs;
    vTaskDelay(pdMS_TO_TICKS(delayMs));
  }
}
}  // namespace

bool lvgl_port_init(LCD* lcd, Touch* touch) {
  if (lcd == nullptr) {
    Serial.println("LVGL init: LCD driver is unavailable");
    return false;
  }
  if (lcd->getRefreshPanelHandle() == nullptr) {
    Serial.println("LVGL init: LCD panel handle is unavailable");
    return false;
  }

  lvglMemoryPool = heap_caps_malloc(LV_MEM_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!lvglMemoryPool) {
    Serial.printf("LVGL init: cannot reserve %u-byte PSRAM widget pool\n",
                  static_cast<unsigned>(LV_MEM_SIZE));
    return false;
  }
  Serial.printf("LVGL widget/mask pool: %u bytes in PSRAM\n",
                static_cast<unsigned>(LV_MEM_SIZE));
  lv_init();
  // Preserve LVGL's alignment, stride, copy and cache callbacks. Install
  // before creating any draw buffers so allocation/free always use one heap.
  // LVGL initializes these three handler sets independently. Updating only
  // the general handlers leaves glyph/image buffers in the small widget pool.
  lv_draw_buf_handlers_t* bufferHandlers[] = {
      lv_draw_buf_get_handlers(), lv_draw_buf_get_font_handlers(),
      lv_draw_buf_get_image_handlers()};
  for (lv_draw_buf_handlers_t* handlers : bufferHandlers) {
    handlers->buf_malloc_cb = allocateRenderBuffer;
    handlers->buf_free_cb = freeRenderBuffer;
  }
  Serial.println("LVGL render, font and image buffers: PSRAM preferred");

  const esp_timer_create_args_t timerArgs = {
      .callback = tickIncrement,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "LVGL tick",
      .skip_unhandled_events = true,
  };
  if (esp_timer_create(&timerArgs, &tickTimer) != ESP_OK ||
      esp_timer_start_periodic(tickTimer, kTickPeriodMs * 1000) != ESP_OK) {
    Serial.println("LVGL init: tick timer creation failed");
    return false;
  }

  const uint32_t width = lcd->getFrameWidth();
  const uint32_t height = lcd->getFrameHeight();
  if (width == 0 || height == 0) {
    Serial.printf("LVGL init: invalid display size %lux%lu\n",
                  static_cast<unsigned long>(width),
                  static_cast<unsigned long>(height));
    return false;
  }
  const size_t bufferBytes = width * kBufferLines * 2;
  // The RGB panel consumes a large amount of internal RAM before LVGL starts.
  // These buffers are CPU-rendered and copied by drawBitmap(), so they can live
  // in PSRAM. Fall back to internal RAM for boards without working PSRAM.
  drawBuffers[0] = heap_caps_malloc(
      bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  drawBuffers[1] = heap_caps_malloc(
      bufferBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (drawBuffers[0] == nullptr || drawBuffers[1] == nullptr) {
    for (void*& buffer : drawBuffers) {
      heap_caps_free(buffer);
      buffer = nullptr;
    }
    drawBuffers[0] = heap_caps_malloc(
        bufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    drawBuffers[1] = heap_caps_malloc(
        bufferBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (drawBuffers[0] == nullptr || drawBuffers[1] == nullptr) {
    Serial.printf(
        "LVGL init: two %u-byte draw buffers unavailable "
        "(internal largest=%u, PSRAM largest=%u)\n",
        static_cast<unsigned>(bufferBytes),
        static_cast<unsigned>(heap_caps_get_largest_free_block(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
    return false;
  }
  Serial.printf("LVGL init: display %lux%lu, draw buffers %u bytes each\n",
                static_cast<unsigned long>(width),
                static_cast<unsigned long>(height),
                static_cast<unsigned>(bufferBytes));

  display = lv_display_create(width, height);
  if (display == nullptr) {
    Serial.println("LVGL init: display creation failed");
    return false;
  }
  lv_display_set_default(display);
  lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
  lv_display_set_user_data(display, lcd);
  lv_display_set_flush_cb(display, flushDisplay);
  lv_display_set_buffers(display, drawBuffers[0], drawBuffers[1], bufferBytes,
                         LV_DISPLAY_RENDER_MODE_PARTIAL);

  if (touch != nullptr) {
    if (touch->isInterruptEnabled()) {
      touchDetected = xSemaphoreCreateBinary();
      if (touchDetected == nullptr) {
        Serial.println("LVGL init: touch semaphore creation failed");
        return false;
      }
      touch->attachInterruptCallback(onTouchInterrupt, touch);
    }
    pointerDevice = lv_indev_create();
    if (pointerDevice == nullptr) {
      Serial.println("LVGL init: touch input creation failed");
      return false;
    }
    lv_indev_set_type(pointerDevice, LV_INDEV_TYPE_POINTER);
    lv_indev_set_user_data(pointerDevice, touch);
    lv_indev_set_read_cb(pointerDevice, readTouch);
  }

  lvglMutex = xSemaphoreCreateRecursiveMutex();
  if (lvglMutex == nullptr) {
    Serial.println("LVGL init: mutex creation failed");
    return false;
  }
  const BaseType_t result = xTaskCreatePinnedToCore(
      lvglTask, "lvgl", kTaskStackSize, nullptr, kTaskPriority,
      &lvglTaskHandle, ARDUINO_RUNNING_CORE);
  if (result != pdPASS) {
    Serial.println("LVGL init: render task creation failed");
    return false;
  }
  return true;
}

bool lvgl_port_lock(int timeoutMs) {
  if (lvglMutex == nullptr) return false;
  const TickType_t timeout =
      timeoutMs < 0 ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
  return xSemaphoreTakeRecursive(lvglMutex, timeout) == pdTRUE;
}

bool lvgl_port_unlock() {
  if (lvglMutex == nullptr) return false;
  return xSemaphoreGiveRecursive(lvglMutex) == pdTRUE;
}

void lvgl_port_set_display_sleeping(bool sleeping) {
  displaySleeping.store(sleeping, std::memory_order_relaxed);
  if (sleeping) wakeRequestPending.store(false, std::memory_order_relaxed);
}

bool lvgl_port_take_touch_activity() {
  return touchActivityPending.exchange(false, std::memory_order_relaxed);
}

bool lvgl_port_take_wake_request() {
  return wakeRequestPending.exchange(false, std::memory_order_relaxed);
}

bool lvgl_port_deinit() {
  if (lvglTaskHandle != nullptr) {
    vTaskDelete(lvglTaskHandle);
    lvglTaskHandle = nullptr;
  }
  if (tickTimer != nullptr) {
    esp_timer_stop(tickTimer);
    esp_timer_delete(tickTimer);
    tickTimer = nullptr;
  }
  lv_deinit();
  heap_caps_free(lvglMemoryPool);
  lvglMemoryPool = nullptr;
  for (void*& buffer : drawBuffers) {
    heap_caps_free(buffer);
    buffer = nullptr;
  }
  if (touchDetected != nullptr) {
    vSemaphoreDelete(touchDetected);
    touchDetected = nullptr;
  }
  if (lvglMutex != nullptr) {
    vSemaphoreDelete(lvglMutex);
    lvglMutex = nullptr;
  }
  display = nullptr;
  pointerDevice = nullptr;
  return true;
}
