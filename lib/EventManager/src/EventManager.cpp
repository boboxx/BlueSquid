#include "EventManager.h"

#include "Logging.h"

namespace {
constexpr char kTag[] = "Events";
}

bool EventManager::subscribe(EventType type, Handler handler) {
  if (!handler || subscriberCount_ >= subscriptions_.size()) {
    LOG_WARN(kTag, "Unable to add subscriber");
    return false;
  }

  subscriptions_[subscriberCount_++] = Subscription(type, std::move(handler));
  return true;
}

void EventManager::publish(const Event& event) const {
  for (size_t index = 0; index < subscriberCount_; ++index) {
    const auto& subscription = subscriptions_[index];
    if (subscription.type == event.type && subscription.handler) {
      subscription.handler(event);
    }
  }
}
