#pragma once

#include <cstdint>
#include <functional>

#include "emu/memory/memory_map.h"
#include "emu/trap/stack_helpers.h"

namespace cyder {

class Emulator {
 public:
  using NativeFunc = std::function<void()>;

  static Emulator& Instance();
  virtual ~Emulator() = default;

  // Initializes registers for Mac OS emulation.
  // Expects the Segment Loader to have already loaded the initial segument and
  // determined the initial program counter (PC) and A5 world position.
  virtual void Init(unsigned int pc) = 0;

  // Runs the emulator for a single timeslice (1000 cycles) or until a native
  // function is encountered (ending the in-progress timeslice).
  virtual void Run() = 0;

  // Writes `NOP` to the given address and registers a native function
  // to be called when the emulator reaches that address during execution.
  // Native functions are expected to perform an `RTS` (return from subroutine)
  // before returning control to the emulator (see `ReturnSubroutine()`).
  virtual void RegisterNativeFunction(uint32_t address, NativeFunc func) = 0;

  // Specialized form of `RegisterNativeFunction()` that registers a handler
  // for A-Traps. This handler will be called when an A-Trap is encountered
  // during emulation. It is expected that the handler will perform the
  // necessary trap handling and return control to the emulator.
  virtual void RegisterATrapHandler(NativeFunc handler) = 0;

  // Registers `func` to run when `memory::kEndFunctionCallAddress` is invoked.
  // Multiple `func`s can be registered and run in FILO (stack) ordering. This
  // is used by `CallFunction<>()` to end functions (accounts for nesting).
  virtual void RegisterExitFunction(NativeFunc func) = 0;
};

// Emulates the `RTS` (return from subroutine) instruction for native code.
void ReturnSubroutine();

// Calls an emulated function at `func_entry` using Pascal calling conventions.
//   NOTE: Use `core/literal_helpers.h` to ensure `args` are the right type. :^)
template <typename ReturnType, typename... Args>
ReturnType CallFunction(uint32_t func_entry, Args&&... args) {
  trap::Push<uint32_t>(m68k_get_reg(NULL, M68K_REG_PC));

  // The emulated function should end with `RTS` which will pop the address of
  // the "calling" function off the stack and jump to it. We want to "jump" back
  // to native code so we push `memory::kEndFunctionCallAddress` to `RTS` to
  // which ends the current emulator timeslice and allows us to grab the return
  // value off the stack and restore the actual `PC` saved above.
  bool function_has_returned = false;
  Emulator::Instance().RegisterExitFunction(
      [&function_has_returned]() { function_has_returned = true; });

  trap::Push<ReturnType>(0);  // Placeholder for return value
  (trap::Push(std::forward<Args>(args)), ...);
  trap::Push<uint32_t>(memory::kEndFunctionCallAddress);
  m68k_set_reg(M68K_REG_PC, func_entry);

  while (!function_has_returned) {
    Emulator::Instance().Run();
  }
  auto return_type = trap::Pop<ReturnType>();
  ReturnSubroutine();
  return return_type;
}

}  // namespace cyder
