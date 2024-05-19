// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <list>

#include "emu/graphics/grafport_types.tdef.h"

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

constexpr Integer kMouseMove = 255;  // Custom `EventType` for native

// Implements the event queue consumed by the MacOS application.
class EventManager final {
 public:
  EventManager();

  static EventManager& the();

  void QueueWindowActivate(Ptr window);
  void QueueWindowUpdate(Ptr window);
  void QueueMouseDown(int x, int y);
  void QueueMouseUp(int x, int y);
  void QueueKeyDown();

  void QueueRawEvent(uint16_t raw_event_type, uint32_t message);

  EventRecord GetNextEvent(uint16_t event_mask);
  uint32_t NowTicks() const;

  bool HasMouseEvent(EventType type) const;

  void OnMouseMove(int x, int y);

  void RegisterNativeListener(std::function<void(EventRecord)> listener);

 private:
  void QueueOrDispatchInputEvent(EventRecord record);

  std::list<EventRecord> activate_events_;
  std::list<EventRecord> input_events_;
  std::list<EventRecord> update_events_;

  std::function<void(EventRecord)> native_listener_ = nullptr;
};

}  // namespace cyder