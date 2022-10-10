#include "memory_map.h"

#include <iomanip>
#include <sstream>

#include "core/logging.h"
#include "global_names.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace memory {
namespace {

// TODO: Add proper verbose logging support to core/logging.h
constexpr bool verbose_logging = false;

// Stores the size above/below the A5 World (used for bounds checking)
uint32_t above_a5_size{0};
uint32_t below_a5_size{0};
uint32_t a5_world{0};

// Stores whether a given address is initialized (written to)
bool kHasInitializedMemory[kSystemMemorySize];

}  // namespace

uint8_t kSystemMemoryRaw[kSystemMemorySize];
core::MemoryRegion kSystemMemory(&kSystemMemoryRaw, kSystemMemorySize);

uint32_t GetA5WorldPosition() {
  return a5_world;
}

absl::Status SetA5WorldBounds(uint32_t above_a5, uint32_t below_a5) {
  above_a5_size = above_a5;
  below_a5_size = below_a5;
  a5_world = kStackStart + below_a5_size;

  if (above_a5_size + a5_world > kLastEmulatedSubroutineAddress) {
    return absl::FailedPreconditionError(absl::StrCat(
        "A5 World is too large for available memory by ",
        (above_a5_size + a5_world - kLastEmulatedSubroutineAddress), " bytes"));
  }

  return absl::OkStatus();
}

void CheckReadAccess(uint32_t address) {
  auto within_region = [&](size_t lower, size_t upper) {
    return address >= lower && address < upper;
  };

  // Interrupt Vector Table
  if (within_region(0, kInterruptVectorTableEnd)) {
    return;
  }

  // System Globals
  if (within_region(kSystemGlobalsLowStart, kSystemGlobalsLowEnd) ||
      within_region(kSystemGlobalsHighStart, kSystemGlobalsHighEnd)) {
    LOG(WARNING) << "Read system global at 0x" << std::hex << address << ": "
                 << GetGlobalVarName(address);
    return;
  }

  // System A-Trap Table
  if (within_region(kSystemTrapTableStart, kSystemTrapTableEnd)) {
    LOG(FATAL) << "Read system A-Trap table directly: 0x" << std::hex
               << address;
    return;
  }

  // Toolbox A-Trap Table
  if (within_region(kToolboxTrapTableStart, kToolboxTrapTableEnd)) {
    LOG(FATAL) << "Read toolbox A-Trap table directly: 0x" << std::hex
               << address;
    return;
  }

  // System Heap
  if (within_region(kSystemHeapStart, kSystemHeapEnd)) {
    if (kHasInitializedMemory[address])
      return;

    LOG(WARNING) << "Read system heap: 0x" << std::hex << address;
    return;
  }

  // Application Heap
  if (within_region(kHeapStart, kHeapEnd)) {
    return;
  }

  // Stack
  if (within_region(kStackEnd, kStackStart)) {
    LOG_IF(INFO, verbose_logging) << "Read Stack: 0x" << std::hex << address
                                  << " (0x" << (kStackStart - address) << ")";
    return;
  }

  // A5 World
  if (address == a5_world) {
    LOG(WARNING) << "Read A5 (Pointer to QuickDraw): 0x" << std::hex << address;
    return;
  }
  if (within_region(a5_world - below_a5_size, a5_world)) {
    LOG_IF(INFO, verbose_logging) << "Read below A5: 0x" << std::hex << address
                                  << " (-0x" << (a5_world - address) << ")";
    if (kHasInitializedMemory[address]) {
      return;
    }
    LOG(WARNING) << "Read un-initialized below A5: 0x" << std::hex << address
                 << " (-0x" << (a5_world - address) << ")";
    return;
  }
  if (within_region(a5_world, a5_world + above_a5_size)) {
    if (address < a5_world + 32) {
      LOG(WARNING) << "Read unimplemented application parameters: 0x"
                   << std::hex << address << " (0x" << (address - a5_world)
                   << ")";
      return;
    }
    LOG_IF(INFO, verbose_logging) << "Read above A5: 0x" << std::hex << address
                                  << " (+0x" << (address - a5_world) << ")";
    return;
  }

  if (address >= kLastEmulatedSubroutineAddress) {
    return;
  }

  LOG(FATAL) << "Untracked read: 0x" << std::hex << address;
}

void CheckWriteAccess(uint32_t address, uint32_t value) {
  auto within_region = [&](size_t lower, size_t upper) {
    return address >= lower && address < upper;
  };

  // Interrupt Vector Table
  CHECK(!within_region(0, 0x100))
      << "IVT is read-only: 0x" << std::hex << address;

  // System Globals
  if (within_region(kSystemGlobalsLowStart, kSystemGlobalsLowEnd) ||
      within_region(kSystemGlobalsHighStart, kSystemGlobalsHighEnd)) {
    LOG(WARNING) << "Write system global at 0x" << std::hex << address << ": "
                 << GetGlobalVarName(address) << " = 0x" << value;
    return;
  }

  // System A-Trap Table
  if (within_region(kSystemTrapTableStart, kSystemTrapTableEnd)) {
    LOG(FATAL) << "Write system A-Trap table directly: 0x" << std::hex
               << address << " = 0x" << value;
    return;
  }

  // Toolbox A-Trap Table
  if (within_region(kToolboxTrapTableStart, kToolboxTrapTableEnd)) {
    LOG(FATAL) << "Write toolbox A-Trap table directly: 0x" << std::hex
               << address << " = 0x" << value;
    return;
  }

  // System Heap
  if (within_region(kSystemHeapStart, kSystemHeapEnd)) {
    if (kHasInitializedMemory[address])
      return;

    LOG(WARNING) << "Write to system heap: 0x" << std::hex << address << " = 0x"
                 << value;
    kHasInitializedMemory[address] = true;
    return;
  }

  // Application Heap
  if (within_region(kHeapStart, kHeapEnd)) {
    LOG(WARNING) << "Write to application heap: 0x" << std::hex << address
                 << " = 0x" << value;
    return;
  }

  // Stack
  if (within_region(kStackEnd, kStackStart)) {
    LOG_IF(INFO, verbose_logging)
        << "Write Stack: 0x" << std::hex << address << " (0x"
        << (kStackStart - address) << ") = 0x" << value;
    return;
  }

  // A5 World
  if (address == a5_world) {
    LOG(WARNING) << "Write A5 (Pointer to QuickDraw): 0x" << std::hex << address
                 << " = 0x" << value;
    return;
  }
  if (within_region(a5_world - below_a5_size, a5_world)) {
    LOG_IF(INFO, verbose_logging)
        << "Write below A5 (app globals): 0x" << std::hex << address << " (-0x"
        << (a5_world - address) << ") = 0x" << value;
    kHasInitializedMemory[address] = true;
    return;
  }
  if (within_region(a5_world, a5_world + above_a5_size)) {
    if (address < a5_world + 32) {
      LOG(WARNING) << "Write unimplemented application parameters: 0x"
                   << std::hex << address << " (0x" << (address - a5_world)
                   << ") = 0x" << value;
      return;
    }
    LOG(WARNING) << "Write above A5: 0x" << std::hex << address << " (+0x"
                 << (address - a5_world) << ") = 0x" << value;
    return;
  }

  if (address > kLastEmulatedSubroutineAddress) {
    LOG(FATAL) << "Writing to address reserved for native function calls: 0x"
               << std::hex << address << " = 0x" << value;
    return;
  }

  LOG(FATAL) << "Untracked write: 0x" << std::hex << address << " = 0x"
             << value;
}

std::string MemoryMapToStr() {
  std::stringstream ss;
  ss << std::hex;
  ss << "Heap: [0x" << kHeapStart << ", 0x" << kHeapEnd << "] "
     << "Stack: [0x" << kStackEnd << ", 0x" << kStackStart << "] "
     << "A5 World: 0x" << a5_world << " (+" << above_a5_size << ", -"
     << below_a5_size << ")";
  return ss.str();
}

namespace debug {

void LogA5World() {
  LOG(INFO) << "A5 World:\n"
            << MUST(kSystemMemory.Create("A5 World", a5_world - below_a5_size,
                                         below_a5_size + above_a5_size));
}

void LogAppGlobals() {
  LOG(INFO) << "Application Globals:\n"
            << MUST(kSystemMemory.Create("Globals", a5_world - below_a5_size,
                                         below_a5_size));
}

void LogStack() {
  uint32_t stack_head = m68k_get_reg(/*context=*/NULL, M68K_REG_SP);
  LOG(INFO) << "Stack:\n"
            << MUST(memory::kSystemMemory.Create("Stack", stack_head,
                                                 kStackStart - stack_head));
}

}  // namespace debug
}  // namespace memory
}  // namespace cyder