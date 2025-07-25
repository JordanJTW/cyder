// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <mutex>

#include "emu/event_manager.h"

extern bool single_step;

namespace cyder {
namespace {

EventRecord NullEvent() {
  static EventRecord null_event;
  null_event.what = 0 /* nullEvent */;
  return null_event;
}

EventManager* s_instance;

}  // namespace

class EventManager::MouseMoveEnablerImpl
    : public EventManager::MouseMoveEnabler {
 public:
  MouseMoveEnablerImpl(EventManager& manager) : manager_(manager) {
    manager_.AcceptMouseMove();
  }
  ~MouseMoveEnablerImpl() override { manager_.RejectMouseMove(); }

 private:
  EventManager& manager_;
};

EventManager::EventManager() {
  s_instance = this;
}

// static
EventManager& EventManager::the() {
  return *s_instance;
}

void EventManager::QueueWindowActivate(Ptr window, ActivateState state) {
  EventRecord record;
  record.what = kWindowActivate;
  record.when = NowTicks();
  record.message = window;
  // For activate events, the value of bit 0 is 1 if the window pointed to by
  // the event message should be activated, and the value is 0 if the window
  // should be deactivated.
  // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-37.html#MARKER-9-85
  record.modifiers = state == ActivateState::ON ? 1 : 0;

  std::lock_guard<std::mutex> lock(event_mutex_);
  activate_events_.push_back(std::move(record));
  event_condition_.notify_all();
}

void EventManager::QueueWindowUpdate(Ptr window) {
  EventRecord record;
  record.what = kWindowUpdate;
  record.when = NowTicks();
  record.message = window;

  std::lock_guard<std::mutex> lock(event_mutex_);
  update_events_.push_back(std::move(record));
  event_condition_.notify_all();
}

void EventManager::QueueMouseDown(int x, int y) {
  Point where;
  where.x = x;
  where.y = y;

  EventRecord record;
  record.what = kMouseDown;
  record.where = where;
  record.when = NowTicks();

  std::lock_guard<std::mutex> lock(event_mutex_);
  input_events_.push_back(std::move(record));
  event_condition_.notify_all();
}

void EventManager::QueueMouseUp(int x, int y) {
  Point where;
  where.x = x;
  where.y = y;

  EventRecord record;
  record.what = kMouseUp;
  record.where = where;
  record.when = NowTicks();

  std::lock_guard<std::mutex> lock(event_mutex_);
  input_events_.push_back(std::move(record));
  event_condition_.notify_all();
}

void EventManager::QueueKeyDown() {
  EventRecord record;
  record.what = kKeyDown;
  record.when = NowTicks();
  // FIXME: Add keycode information in EventRecord::message

  std::lock_guard<std::mutex> lock(event_mutex_);
  input_events_.push_back(std::move(record));
  event_condition_.notify_all();
}

void EventManager::QueueRawEvent(uint16_t raw_event_type, uint32_t message) {
  // This exists to implement PostEvent() which allows the emulated application
  // to inject events into the queue... This seems to be fairly unusual and
  // even more unusual is that "1000 Miles" uses PostEvent() to publish a
  // custom undocumented event to trigger the AI player to take its turn.
  // This sort of use is _heavily_ discouraged in the documentation.

  EventRecord record;
  record.what = raw_event_type;
  record.when = NowTicks();
  record.message = message;
  // Even stanger is that this should go into the "Low-Level OS Event Queue"
  // i.e. `input_events_` however it _only_ works in "1000 Miles" if it is in
  // _this_ queue... This requires some more investigation but works for now.
  std::lock_guard<std::mutex> lock(event_mutex_);
  activate_events_.push_back(std::move(record));
}

EventRecord EventManager::WaitNextEvent(uint16_t event_mask, uint32_t timeout) {
  EventRecord current_event = GetNextEvent(event_mask);
  if (current_event.what != kNullEvent || timeout == 0) {
    return current_event;
  }

  {
    auto has_event_pred = [this, event_mask]() {
      // Ensure `wait()` does not interpret shutting down as a spurious wake 
      if (is_shutting_down_)
        return true;

      // Check if any event matching the mask is available
      if (!activate_events_.empty() && (event_mask & 256 /*activMask*/)) {
        return true;
      }
      if (!input_events_.empty() &&
          (event_mask & (2 /*mDownMask*/ | 4 /*mUpMask*/ | 8 /*keyDownMask*/ |
                         16 /*keyUpMask*/))) {
        auto is_event_enabled = [event_mask](const EventRecord& event) {
          return event_mask & (1 << event.what);
        };
        return std::find_if(input_events_.begin(), input_events_.end(),
                            is_event_enabled) != input_events_.end();
      }
      if (!update_events_.empty() && (event_mask & 64 /*updateMask*/)) {
        return true;
      }
      return false;
    };

    std::unique_lock<std::mutex> lock(event_mutex_);
    // If the EventManager is already shutting down do not bother `wait()`ing.
    if (is_shutting_down_)
      return NullEvent();

#if true
    bool event_available =
        event_condition_.wait_for(lock, std::chrono::milliseconds(timeout * 16),
                                  std::move(has_event_pred));
    if (!event_available) {
      return NullEvent();
    }
#else
    event_condition_.wait(lock, std::move(has_event_pred));
#endif
  }
  // Try again to get the event after being notified
  return GetNextEvent(event_mask);
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
  std::lock_guard<std::mutex> lock(event_mutex_);

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
      EventRecord record = *event_iter;
      input_events_.erase(event_iter);
      return record;
    }
  }

  if (!update_events_.empty() && (event_mask & 64 /*updateMask*/)) {
    auto event = update_events_.front();
    update_events_.pop_front();
    return event;
  }
  return NullEvent();
}

uint32_t EventManager::NowTicks() const {
  // TODO: Read from GlobalVar::Ticks which should be set every 1/60th secs
  static absl::Time boot_time = absl::Now();
  auto elapsed_time_ms = absl::ToInt64Milliseconds(absl::Now() - boot_time);
  return elapsed_time_ms / 16;
}

bool EventManager::HasMouseEvent(EventType type) const {
  CHECK(type == kMouseDown || type == kMouseUp);

  std::lock_guard<std::mutex> lock(event_mutex_);
  return std::find_if(input_events_.begin(), input_events_.end(),
                      [type](const EventRecord& event) {
                        return event.what == type;
                      }) != input_events_.end();
}

std::unique_ptr<EventManager::MouseMoveEnabler>
EventManager::EnableMouseMove() {
  return std::make_unique<MouseMoveEnablerImpl>(*this);
}

void EventManager::OnMouseMove(int x, int y) {
  Point where;
  where.x = x;
  where.y = y;

  EventRecord record;
  record.what = kMouseMove;
  record.where = where;
  record.when = NowTicks();

  std::lock_guard<std::mutex> lock(event_mutex_);
  if (mouse_move_enabled_) {
    input_events_.push_back(std::move(record));
  }
}

void EventManager::PrintEvents() const {
  std::lock_guard<std::mutex> lock(event_mutex_);
  for (const auto& event : activate_events_) {
    LOG(INFO) << "Activate Event: " << event.message << " at " << event.when;
  }
  for (const auto& event : input_events_) {
    LOG(INFO) << "Input Event: " << event.what << " at " << event.when;
  }
  for (const auto& event : update_events_) {
    LOG(INFO) << "Update Event: " << event.message << " at " << event.when;
  }
}

}  // namespace cyder