#include "emu/emulator.h"

#include <cstdint>
#include <functional>
#include <map>

#include "absl/base/no_destructor.h"
#include "absl/types/optional.h"
#include "core/logging.h"
#include "emu/memory/memory_map.h"
#include "emu/trap/stack_helpers.h"
#include "third_party/musashi/src/m68k.h"

extern "C" {

void cpu_instr_callback(unsigned int pc);

}  // extern "C"

namespace cyder {
namespace {

constexpr unsigned int kCpuType = M68K_CPU_TYPE_68030;
constexpr bool kDisassembleInstructions = false;

}  // namespace

class EmulatorImpl : public Emulator {
 public:
  EmulatorImpl() {
    m68k_init();
    m68k_set_instr_hook_callback(cpu_instr_callback);
    m68k_set_cpu_type(kCpuType);
  }

  void Init(unsigned int pc) override {
    m68k_set_reg(M68K_REG_PC, pc);
    m68k_set_reg(M68K_REG_A5, memory::GetA5WorldPosition());
    m68k_set_reg(M68K_REG_SP, memory::kStackStart);

    // Mac OS _always_ runs in supervisor mode so set the SR
    // Link: https://en.wikibooks.org/wiki/68000_Assembly/Registers
    uint32_t sr = m68k_get_reg(NULL, M68K_REG_SR);
    m68k_set_reg(M68K_REG_SR, sr | (1 << 13));
  }

  void Run() override {
    m68k_execute(1000);

    if (!native_func_.has_value())
      return;

    (*native_func_)();
    native_func_.reset();
  }

  void RegisterNativeFunction(uint32_t address, NativeFunc func) override {
    CHECK_OK(memory::kSystemMemory.Write<uint16_t>(address, 0x4E71 /*NOP*/))
        << "Unable to write NOP to address 0x" << std::hex << address;
    native_functions_[address] = std::move(func);
  }

  void RegisterATrapHandler(NativeFunc handler) override {
    // Sets up the A-Trap exception handler to call a subroutine containing RTE.
    // AFTER this subroutine is called, the emulator will end the current
    // timeslice and call the native TrapManager::PerformTrapEntry().
    // NOTE: The stack will be different than on a real machine since the RTE
    //       is executed BEFORE the A-Trap handler is called.
    RegisterNativeFunction(0x1fff, std::move(handler));
    CHECK_OK(memory::kSystemMemory.Write<uint32_t>(0x28, 0x1fff));
    // Overwrite the native function `NOP` with an `RTE` instruction to restore
    // the PC and SR registers correctly.
    CHECK_OK(memory::kSystemMemory.Write<uint16_t>(0x1fff, 0x4E73 /* RTE */));
  }

  void HandleInstruction(unsigned int address) {
    CHECK_NE(address, 0) << "Reset";

    // Check that the stack pointer is within the bounds of the stack.
    CHECK(m68k_get_reg(NULL, M68K_REG_ISP) <= cyder::memory::kStackStart);
    CHECK(m68k_get_reg(NULL, M68K_REG_ISP) > cyder::memory::kStackEnd);

    // Only one native function should be queued at a time since one being
    // encountered MUST end the timeslice.
    CHECK(!native_func_.has_value());
    if (native_functions_.find(address) != native_functions_.end()) {
      native_func_ = native_functions_[address];
      m68k_end_timeslice();
    }

    if constexpr (kDisassembleInstructions) {
      char buffer[255];
      m68k_disassemble(buffer, address, kCpuType);
      printf("0x%x: %s\n", address, buffer);
    }
  }

 private:
  ~EmulatorImpl() override = default;

  std::map<uint32_t, NativeFunc> native_functions_;
  absl::optional<NativeFunc> native_func_;
};

// static
Emulator& Emulator::Instance() {
  static absl::NoDestructor<EmulatorImpl> instance;
  return *instance;
}

void ReturnSubroutine() {
  // Emulates an RTS instruction by restoring the previous PC from the stack.
  m68k_set_reg(M68K_REG_PC, trap::Pop<uint32_t>());
}

}  // namespace cyder

using ::cyder::memory::kSystemMemory;

extern "C" {

// Musashi memory read/write functions and cpu instruction callback.
unsigned int m68k_read_disassembler_8(unsigned int address) {
  return MUST(kSystemMemory.Read<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return MUST(kSystemMemory.Read<uint16_t>(address));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return MUST(kSystemMemory.Read<uint32_t>(address));
}
unsigned int m68k_read_memory_8(unsigned int address) {
  // cyder::memory::CheckReadAccess(address);
  return MUST(kSystemMemory.Read<uint8_t>(address));
}
unsigned int m68k_read_memory_16(unsigned int address) {
  // cyder::memory::CheckReadAccess(address);
  return MUST(kSystemMemory.Read<uint16_t>(address));
}
unsigned int m68k_read_memory_32(unsigned int address) {
  // cyder::memory::CheckReadAccess(address);
  return MUST(kSystemMemory.Read<uint32_t>(address));
}
void m68k_write_memory_8(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  CHECK_OK(kSystemMemory.Write<uint8_t>(address, value))
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  CHECK_OK(kSystemMemory.Write<uint16_t>(address, value))
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  cyder::memory::CheckWriteAccess(address, value);
  CHECK_OK(kSystemMemory.Write<uint32_t>(address, value))
      << " unable to write " << std::hex << value << " to " << address;
}
void cpu_instr_callback(unsigned int pc) {
  static_cast<cyder::EmulatorImpl&>(cyder::Emulator::Instance())
      .HandleInstruction(pc);
}

}  // extern "C"
