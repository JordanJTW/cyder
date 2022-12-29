#pragma once

#include <cstdint>

#include "core/literal_helpers.h"
#include "core/memory_region.h"
#include "gen/typegen/typegen_prelude.h"

namespace cyder {
namespace memory {

extern core::MemoryRegion kSystemMemory;

const size_t kSystemMemorySize = 512_kb;
const size_t kDefaultStackSize = 4_kb;

// Defines the memory map exposed to the emulated m68k; it should
// be noted that when there are multiple "correct" locations (depending
// on the model of Macintosh) the _largest_ option is always used below
// to try to catch reads/writes in those locations.
// Link: http://www.mac.linux-m68k.org/devel/macalmanac.php
//
//  [ HIGH MEMORY ]
//  Exception Return Addr: 'RTE'
//
//    ... 4 KB Buffer for A5 World to grow up into
//  Jump Table (above A5)
//  A5 World (A5)
//  Application & QuickDraw Globals (below A5)
//
//  Stack (A7)
//   ... Default Stack Size
//  End of Stack
//  End of Application Heap
//
//  Application Heap (ApplZone)
//
//  0x1C00 System Heap (SysZone)
//
//  0x0C00 Toolbox A-Trap Table
//
//  0x0800 System Globals
//
//  0x0400 System A-Trap Table
//
//  0x0100 System Globals
//
//  0x00FF End of Interrupt Vector Table
//  [ LOW MEMORY ]

const size_t kTrapManagerEntryAddress = kSystemMemorySize - sizeof(uint16_t);
const size_t kTrapManagerExitAddress =
    kTrapManagerEntryAddress - sizeof(uint32_t);

const size_t kBaseSystemTrapAddress =
    kTrapManagerExitAddress - (256 * sizeof(uint16_t));
const size_t kBaseToolboxTrapAddress =
    kBaseSystemTrapAddress - (1024 * sizeof(uint16_t));

const size_t kLastEmulatedSubroutineAddress = kBaseToolboxTrapAddress;

// A5 World

// User Stack
const size_t kStackStart = kSystemMemorySize - 32_kb;
const size_t kStackEnd = kStackStart - kDefaultStackSize;

// System Heap
const size_t kSystemHeapStart = 0x1C00;
const size_t kSystemHeapEnd = kSystemHeapStart + 4_kb;

// Application Heap
const size_t kHeapStart = kSystemHeapEnd;
const size_t kHeapEnd = kStackEnd;

// Toolbox A-Trap Table
const size_t kToolboxTrapTableEnd = 0x1C00;
const size_t kToolboxTrapTableStart = 0x0C00;

// System Globals
const size_t kSystemGlobalsHighEnd = 0x0C00;
const size_t kSystemGlobalsHighStart = 0x0800;

// System A-Trap Table
const size_t kSystemTrapTableEnd = 0x07FF;
const size_t kSystemTrapTableStart = 0x0400;

// System Globals
const size_t kSystemGlobalsLowEnd = 0x0400;
const size_t kSystemGlobalsLowStart = 0x0100;

// Interrupt Vector Table
const size_t kInterruptVectorTableEnd = 0x0100;
const size_t kInterruptVectorTableStart = 0x0000;

// Logs and/or CHECK fails if access to `address` is unexpected such as
// reading/writing in "buffer" regions or reading uninitialized memory.
void CheckReadAccess(uint32_t address);
void CheckWriteAccess(uint32_t address, uint32_t value);

// Sets the bounds of the A5 world when bounds checking.
absl::Status SetA5WorldBounds(uint32_t above_a5, uint32_t below_a5);
uint32_t GetA5WorldPosition();

// Returns a string representation of the memory map (for debugging).
std::string MemoryMapToStr();

void LogRegionAccess(size_t offset,
                     size_t length,
                     bool on_read,
                     bool on_write,
                     const std::string& region_name,
                     std::vector<Field> whitelist_fields);

#define RESTRICT_FIELD_ACCESS(type, address, ...)                         \
  ::cyder::memory::LogRegionAccess(address, type::fixed_size, true, true, \
                                   #type, {__VA_ARGS__});

namespace debug {
void LogA5World();  // Logs the A5 World (from below to above A5)
void LogAppGlobals();
void LogStack(uint32_t stack_head);
}  // namespace debug

}  // namespace memory
}  // namespace cyder