#pragma once

#include <cstdint>
#include <functional>

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
};

// Emulates the `RTS` (return from subroutine) instruction for native code.
void ReturnSubroutine();

}  // namespace cyder
