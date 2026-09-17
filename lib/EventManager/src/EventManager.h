#pragma once

#include <Arduino.h>

#include <array>
#include <functional>
#include <utility>

enum class EventType : uint8_t {
  StatusChanged,
  OutputChanged,
  AlarmRaised,
  SettingsChanged,
};

struct Event {
  Event(EventType eventType, uint32_t timestamp, int32_t eventValue = 0)
      : type(eventType), timestampMs(timestamp), value(eventValue) {}

  EventType type;
  uint32_t timestampMs;
  int32_t value;
};

class EventManager {
 public:
  using Handler = std::function<void(const Event&)>;

  static constexpr size_t kMaxSubscribers = 16;

  bool subscribe(EventType type, Handler handler);
  void publish(const Event& event) const;

 private:
  struct Subscription {
    Subscription() = default;
    Subscription(EventType eventType, Handler eventHandler)
        : type(eventType), handler(std::move(eventHandler)) {}

    EventType type = EventType::StatusChanged;
    Handler handler;
  };

  std::array<Subscription, kMaxSubscribers> subscriptions_{};
  size_t subscriberCount_ = 0;
};
