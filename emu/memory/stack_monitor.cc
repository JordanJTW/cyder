#include "emu/memory/stack_monitor.h"

#include "emu/memory/memory_map.h"

namespace cyder {
namespace memory {

void StackMonitor::MaybeHandleWrite(uint32_t address, int value, size_t size) {
  if (address > kStackStart || address < stack_head()) {
    return;
  }

  // LOG(INFO) << "Write: 0x" << std::hex << address << " = 0x" << value << "["
  //           << std::dec << size << "]";
}

void StackMonitor::UpdateStackState() {}

}  // namespace memory
}  // namespace cyder
