// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "emu/memory/memory_manager.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {

class DebugLogger {
 public:
  void OnInstruction(uint32_t pc);

  void SetMemoryManager(const memory::MemoryManager* memory) {
    memory_ = memory;
  }

 private:
  struct Register {
    m68k_register_t id;
    uint32_t value;
  };

  struct Step {
    uint32_t pc;
    std::vector<Register> registers;
  };

  std::vector<Step> steps_;
  std::unique_ptr<uint8_t[]> last_context_;
  std::unique_ptr<uint8_t[]> last_stack_;

  const memory::MemoryManager* memory_;
};

}  // namespace cyder