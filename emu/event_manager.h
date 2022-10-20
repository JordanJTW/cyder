#pragma once

#include <queue>

#include "emu/graphics/grafport_types.h"

namespace cyder {

class EventManager final {
 public:
  EventManager();

  void QueueEvent(uint16_t what, uint32_t message);

  EventRecord GetNextEvent();

 private:
  std::queue<EventRecord> event_queue_{};
};

}  // namespace cyder