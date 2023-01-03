// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/debug_logger.h"

#include "core/logging.h"
#include "emu/memory/memory_map.h"

namespace cyder {
namespace {

// Track all registers _except_ the stack register (A7)
constexpr m68k_register_t kRegistersToScan[] = {
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3,
    M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, /*M68K_REG_A7,*/
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3,
    M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
};

const char* kRegisterNames[] = {
    "D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7",
    "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
};

}  // namespace

void DebugLogger::OnInstruction(uint32_t pc) {
  auto context = std::make_unique<uint8_t[]>(m68k_context_size());
  m68k_get_context(context.get());

  std::vector<Register> registers;
  if (last_context_) {
    for (auto r : kRegistersToScan) {
      auto current_value = m68k_get_reg(context.get(), r);
      if (current_value != m68k_get_reg(last_context_.get(), r)) {
        registers.push_back({.id = r, .value = current_value});
      }
    }
  }

  if (!registers.empty()) {
    std::stringstream ss;
    for (const auto& r : registers) {
      ss << kRegisterNames[r.id] << " = 0x" << std::hex << r.value << ", ";
    }
    LOG(INFO) << "UPDATE: " << ss.str();
  }

  auto handle = memory_->GetHandleThatContains(pc);
  std::string tag = (handle == 0) ? "" : memory_->GetTag(handle);

  char buffer[255];
  m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
  LOG(INFO) << tag << std::hex << "[0x" << pc << "]: " << buffer;

  steps_.push_back({.pc = pc, .registers = std::move(registers)});
  last_context_ = std::move(context);
}

}  // namespace cyder
