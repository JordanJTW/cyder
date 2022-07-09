#include "memory_map.h"

#include <iomanip>
#include <sstream>

#include "core/logging.h"

uint8_t kSystemMemoryRaw[kSystemMemorySize];
core::MemoryRegion kSystemMemory(&kSystemMemoryRaw, kSystemMemorySize);

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

void SetA5WorldBounds(uint32_t above_a5, uint32_t below_a5) {
  above_a5_size = above_a5;
  below_a5_size = below_a5;
  a5_world = kUserStackStart + below_a5_size;
}

uint32_t GetA5WorldPosition() {
  return a5_world;
}

void CheckReadAccess(uint32_t address) {
  auto within_region = [&](size_t lower, size_t upper) {
    return address >= lower && address < upper;
  };
  auto within_stack = [&](size_t start, size_t end) {
    return address > end && address <= start;
  };

  // Interrupt Vector Table (handled below)
  if (within_region(0, kInterruptVectorTableEnd)) {
    return;
  }

  // Global Variables / OS Reserved
  if (within_region(kInterruptVectorTableEnd, kHeapStart)) {
    // AppName (STR[31]):
    if (within_region(0x910, 0x932)) {
      return;
    }

    LOG(WARNING) << std::hex << "Read un-initialized system global [0x100, 0x"
                 << kHeapStart << "): 0x" << address;
    return;
  }

  // Application Heap
  if (within_region(kHeapStart, kHeapEnd)) {
    return;
  }

  // User Stack
  if (within_stack(kUserStackStart, kUserStackEnd)) {
    LOG_IF(INFO, verbose_logging)
        << "Read User Stack: 0x" << std::hex << address << " (0x"
        << (kUserStackStart - address) << ")";
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

  // Interrupt Stack
  if (within_stack(kInterruptStackStart, kInterruptStackEnd)) {
    LOG_IF(INFO, verbose_logging)
        << "Read Interrupt Stack: 0x" << std::hex << address << " (0x"
        << (kInterruptStackStart - address) << ")";
    return;
  }

  // RTE
  if (address == kExceptionReturnAddr) {
    return;
  }

  LOG(FATAL) << "Untracked read: 0x" << std::hex << address;
}

void CheckWriteAccess(uint32_t address, uint32_t value) {
  auto within_region = [&](size_t lower, size_t upper) {
    return address >= lower && address < upper;
  };
  auto within_stack = [&](size_t start, size_t end) {
    return address > end && address <= start;
  };

  // Interrupt Vector Table
  CHECK(!within_region(0, 0x100))
      << "IVT is read-only: 0x" << std::hex << address;

  // Global Variables / OS Reserved
  if (within_region(0x100, kHeapStart)) {
    LOG(WARNING) << std::hex << "Write system global [0x100, 0x" << kHeapStart
                 << "): 0x" << address << " = 0x" << value;
    return;
  }

  // Application Heap
  if (within_region(kHeapStart, kHeapEnd)) {
    LOG(WARNING) << "Write to application heap: 0x" << std::hex << address;
    return;
  }

  // User Stack
  if (within_stack(kUserStackStart, kUserStackEnd)) {
    LOG_IF(INFO, verbose_logging)
        << "Write User Stack: 0x" << std::hex << address << " (0x"
        << (kUserStackStart - address) << ")";
    return;
  }

  // A5 World
  if (address == a5_world) {
    LOG(WARNING) << "Write A5 (Pointer to QuickDraw): 0x" << std::hex
                 << address;
    return;
  }
  if (within_region(a5_world - below_a5_size, a5_world)) {
    LOG_IF(INFO, verbose_logging)
        << "Write below A5 (app globals): 0x" << std::hex << address << " (-0x"
        << (a5_world - address) << ")";
    kHasInitializedMemory[address] = true;
    return;
  }
  if (within_region(a5_world, a5_world + above_a5_size)) {
    if (address < a5_world + 32) {
      LOG(WARNING) << "Write unimplemented application parameters: 0x"
                   << std::hex << address << " (0x" << (address - a5_world)
                   << ")";
      return;
    }
    LOG(WARNING) << "Write above A5: 0x" << std::hex << address << " (+0x"
                 << (address - a5_world) << ")";
    return;
  }

  // Interrupt Stack
  if (within_stack(kInterruptStackStart, kInterruptStackEnd)) {
    LOG_IF(INFO, verbose_logging)
        << "Write Interrupt Stack: 0x" << std::hex << address << " (0x"
        << (kInterruptStackStart - address) << ")";
    return;
  }

  // RTE
  CHECK(address != kExceptionReturnAddr)
      << "Interrupt return instruction is read-only";

  LOG(FATAL) << "Untracked write: 0x" << std::hex << address;
}

std::string MemoryMapToStr() {
  std::stringstream ss;
  ss << std::hex;
  ss << "Heap: [0x" << kHeapStart << ", 0x" << kHeapEnd << "] "
     << "User Stack: [0x" << kUserStackEnd << ", 0x" << kUserStackStart << "] "
     << "Interrupt Stack: "
     << "[0x" << kInterruptStackEnd << ", 0x" << kInterruptStackStart << "] "
     << "A5 World: 0x" << a5_world << " (+" << above_a5_size << ", -"
     << below_a5_size << ")";
  return ss.str();
}