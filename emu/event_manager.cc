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
  record.what = kWindowActivate;
  record.message = window;
  activate_events_.push_back(std::move(record));
}

void EventManager::QueueWindowUpdate(Ptr window) {
  EventRecord record;
  record.what = kWindowUpdate;
  record.message = window;
  update_events_.push_back(std::move(record));
}

void EventManager::QueueMouseDown(int x, int y) {
  Point where;
  where.x = x;
  where.y = y;

  EventRecord record;
  record.what = kMouseDown;
  record.where = where;
  input_events_.push_back(std::move(record));
}

void EventManager::QueueMouseUp(int x, int y) {
  Point where;
  where.x = x;
  where.y = y;

  EventRecord record;
  record.what = kMouseUp;
  record.where = where;
  input_events_.push_back(std::move(record));
}

void EventManager::QueueKeyDown() {
  EventRecord record;
  record.what = kKeyDown;
  // FIXME: Add keycode information in EventRecord::message
  input_events_.push_back(std::move(record));
}

void EventManager::QueueRawEvent(uint16_t raw_event_type, uint32_t message) {
  // This exists to implement PostEvent() which allows the emulated application
  // to inject events into the queue... This seems to be fairly unusual and
  // even more unusual is that "1000 Miles" uses PostEvent() to publish a
  // custom undocumented event to trigger the AI player to take its turn.
  // This sort of use is _heavily_ discouraged in the documentation.

  EventRecord record;
  record.what = raw_event_type;
  record.message = message;
  // Even stanger is that this should go into the "Low-Level OS Event Queue"
  // i.e. `input_events_` however it _only_ works in "1000 Miles" if it is in
  // _this_ queue... This requires some more investigation but works for now.
  activate_events_.push_back(std::move(record));
}

EventRecord EventManager::GetNextEvent(uint16_t event_mask) {
  // From "Macintosh Toolbox Essentials (Event Manager 2-28)":
  // Masking out specific types of events does not remove those events
  // from the event stream. If a type of event is masked out, the Event
  // Manager simply ignores it when reporting events from the event
  // stream.

  // From "Macintosh Toolbox Essentials (Event Manager 2-15)":
  // Each type of event has a certain priority. The Event Manager returns events
  // in this order of priority:
  //   1. activate events
  //   2. mouse-down, mouse-up, key-down, key-up, and disk-inserted (FIFO order)
  //   3. auto-key events (TODO: What are auto-key events?)
  //   4. update events(in front-to-back order of windows)
  //   5. OS events(suspend, resume, mouse-moved)
  //   6. high-level events (Application IPC)
  //   7. null events

  // Event masks can be calculated from the type and are documented here:
  // http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-38.html#MARKER-9-112

  if (!activate_events_.empty() && (event_mask & 256 /*activMask*/)) {
    auto event = activate_events_.front();
    activate_events_.pop_front();
    return event;
  }

  // This works for all events except NULL(5), OS(6), and HIGH-LEVEL(7) type
  auto is_event_enabled = [event_mask](const EventRecord& event) {
    return event_mask & (1 << event.what);
  };

  if (!input_events_.empty() &&
      (event_mask & (2 /*mDownMask*/ | 4 /*mUpMask*/ | 8 /*keyDownMask*/ |
                     16 /*keyUpMask*/))) {
    auto event_iter = std::find_if(input_events_.begin(), input_events_.end(),
                                   is_event_enabled);
    if (event_iter != input_events_.end()) {
      input_events_.erase(event_iter);
      return *event_iter;
    }
  }

  if (!update_events_.empty() && (event_mask & 64 /*updateMask*/)) {
    auto event = update_events_.front();
    update_events_.pop_front();
    return event;
  }
  return NullEvent();
}

bool EventManager::HasMouseEvent(EventType type) const {
  CHECK(type == kMouseDown || type == kMouseUp);
  return std::find_if(input_events_.begin(), input_events_.end(),
                      [type](const EventRecord& event) {
                        return event.what == type;
                      }) != input_events_.end();
}

}  // namespace cyder