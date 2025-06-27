// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <condition_variable>
#include <list>
#include <mutex>

#include "emu/event_manager.tdef.h"

namespace cyder {

// Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-45.html#MARKER-9-331
enum EventType {
  kNullEvent = 0,       // no other pending events
  kMouseDown = 1,       // mouse button pressed
  kMouseUp = 2,         // mouse button released
  kKeyDown = 3,         // key pressed
  kKeyUp = 4,           // key released
  kAutoKey = 5,         // key repeatedly held down
  kWindowUpdate = 6,    // window needs updating
  kDiskInsert = 7,      // disk inserted
  kWindowActivate = 8,  // activate / deactivate window
};

// Whether a window is being made active (ON) or deactive (OFF).
enum ActivateState { ON, OFF };

// Custom `EventType` for native (9-14 are undefined)
constexpr Integer kMouseMove = 10;

// Implements the event queue consumed by the MacOS application.
class EventManager final {
 public:
  explicit EventManager();

  static EventManager& the();

  void QueueWindowActivate(Ptr window, ActivateState state);
  void QueueWindowUpdate(Ptr window);
  void QueueMouseDown(int x, int y);
  void QueueMouseUp(int x, int y);
  void QueueKeyDown();

  void QueueRawEvent(uint16_t raw_event_type, uint32_t message);

  EventRecord WaitNextEvent(uint16_t event_mask, uint32_t timeout);
  EventRecord GetNextEvent(uint16_t event_mask);
  uint32_t NowTicks() const;

  bool HasMouseEvent(EventType type) const;

  class MouseMoveEnabler {
   public:
    virtual ~MouseMoveEnabler() = default;
  };

  std::unique_ptr<MouseMoveEnabler> EnableMouseMove();
  void OnMouseMove(int x, int y);

  bool has_window_events() const {
    return !activate_events_.empty() || !update_events_.empty();
  }

  void PrintEvents() const;
  void Shutdown() {
    std::lock_guard<std::mutex> lock(event_mutex_);
    is_shutting_down_ = true;
    event_condition_.notify_all();
  }

 private:
  class MouseMoveEnablerImpl;
  void AcceptMouseMove() {
    std::lock_guard<std::mutex> lock(event_mutex_);
    mouse_move_enabled_ = true;
  }
  void RejectMouseMove() {
    std::lock_guard<std::mutex> lock(event_mutex_);
    mouse_move_enabled_ = false;
  }

  mutable std::mutex event_mutex_;  // Protects member variables below.
  std::condition_variable event_condition_;
  std::list<EventRecord> activate_events_;
  std::list<EventRecord> input_events_;
  std::list<EventRecord> update_events_;
  bool mouse_move_enabled_ = false;
  bool is_shutting_down_ = false;
};

}  // namespace cyder