// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "emu/memory/memory_manager.h"

class DebugLogger {
 public:
  void OnInstruction(uint32_t pc);

  void SetMemoryManager(const cyder::memory::MemoryManager* memory) {
    memory_ = memory;
  }

 private:
  struct Register {
    uint8_t id;
    uint32_t value;
  };

  struct Step {
    uint32_t pc;
    std::vector<Register> registers;
  };

  std::vector<Step> steps_;
  std::unique_ptr<uint8_t[]> last_context_;
  std::unique_ptr<uint8_t[]> last_stack_;

  const cyder::memory::MemoryManager* memory_;
};