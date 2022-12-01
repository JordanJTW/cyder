#pragma once

#include <queue>

#include "emu/graphics/grafport_types.tdef.h"

namespace cyder {

// Implements the event queue consumed by the MacOS application.
// Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-45.html#MARKER-9-331
class EventManager final {
 public:
  EventManager();

  void QueueWindowActivate(Ptr window);
  void QueueWindowUpdate(Ptr window);
  void QueueMouseDown(int x, int y);
  void QueueKeyDown();

  EventRecord GetNextEvent();

 private:
  std::queue<EventRecord> event_queue_{};
};

}  // namespace cyder