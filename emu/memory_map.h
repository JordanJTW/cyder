#pragma once

#include <cstdint>
#include <set>

#include "core/literal_helpers.h"
#include "core/memory_region.h"

extern core::MemoryRegion kSystemMemory;

const size_t kSystemMemorySize = 512_kb;
const size_t kDefaultStackSize = 4_kb;

// Defines the memory map exposed to the emulated m68k:
//
//  [ HIGH MEMORY ]
//  Exception Return Addr: 'RTE'
//   ... Buffer (~4KB)
//
//  Interrupt Stack
//   ... Default Stack Size
//  End of Interrupt Stack
//   ... Buffer (~24KB assuming Default Stack Size is 4KB)
//
//  A5 World Addr (Above/Below relative to here)
//   ... Buffer (32KB)
//
//  User Stack
//   ... Default Stack Size
//  End of User Stack
//  End of Application Heap
//   ... Heap size depends on Default Stack Size
//  Application Heap
//   ... Buffer (~32KB)
//
//  0x100 End of Interrupt Vector Table
//  [ LOW MEMORY ]

// Enough room to store a single 'RTE' instruction (this address is
// used in Interrupt Vector Table entries to return to user mode).
const size_t kExceptionReturnAddr = kSystemMemorySize - sizeof(uint16_t);

// Interrupt Stack
const size_t kInterruptStackStart = kSystemMemorySize - 4_kb;
const size_t kInterruptStackEnd = kInterruptStackStart - kDefaultStackSize;

// A5 World
const size_t kA5Position = kSystemMemorySize - 32_kb;

// User Stack
const size_t kUserStackStart = kA5Position - 32_kb;
const size_t kUserStackEnd = kUserStackStart - kDefaultStackSize;

// Application Heap
const size_t kHeapStart = 32_kb;
const size_t kHeapEnd = kUserStackEnd;

// Interrupt Vector Table
const size_t kInterruptVectorTableEnd = 0x100;

// Logs and/or CHECK fails if access to `address` is unexpected such as
// reading/writing in "buffer" regions or reading uninitialized memory.
void CheckReadAccess(uint32_t address);
void CheckWriteAccess(uint32_t address, uint32_t value);

// Sets the bounds of the A5 world when bounds checking.
void SetA5WorldBounds(uint32_t above_a5, uint32_t below_a5);

// Returns a string representation of the memory map (for debugging).
std::string MemoryMapToStr();