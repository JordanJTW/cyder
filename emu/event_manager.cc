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

void EventManager::QueueWindowActivate(Ptr window) {
  EventRecord record;
  record.what = 8 /*activateEvt*/;
  record.message = window;
  event_queue_.push(std::move(record));
}

void EventManager::QueueWindowUpdate(Ptr window) {
  EventRecord record;
  record.what = 6 /*updateEvt*/;
  record.message = window;
  event_queue_.push(std::move(record));
}

void EventManager::QueueMouseDown(int x, int y) {
  Point where;
  where.x = x;
  where.y = y;

  EventRecord record;
  record.what = 1 /*mouseDown*/;
  record.where = std::move(where);
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
