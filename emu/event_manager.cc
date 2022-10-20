#include "emu/event_manager.h"

namespace cyder {
namespace {

EventRecord NullEvent() {
  static EventRecord null_event;
  null_event.what = 0 /* nullEvent */;
  return null_event;
}

}  // namespace

EventManager::EventManager() = default;

void EventManager::QueueEvent(uint16_t what, uint32_t message) {
  EventRecord record;
  record.what = what;
  record.message = message;
  event_queue_.push(std::move(record));
}

EventRecord EventManager::GetNextEvent() {
  if (event_queue_.empty()) {
    return NullEvent();
  }

  auto front = event_queue_.front();
  event_queue_.pop();
  return front;
}

}  // namespace cyder
