#pragma once

#include <cstddef>
#include <cstdint>

#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace memory {

class StackMonitor {
 public:
  void MaybeHandleWrite(uint32_t address, int value, size_t size);
  void UpdateStackState();

 private:
  uint32_t stack_head() const {
    return m68k_get_reg(/*context=*/NULL, M68K_REG_A7);
  }
};

}  // namespace memory
}  // namespace cyder
