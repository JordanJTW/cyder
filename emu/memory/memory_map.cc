// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "memory_map.h"

#include <iomanip>
#include <sstream>

#include "absl/strings/str_cat.h"
#include "core/logging.h"
#include "emu/debug/debug_manager.h"
#include "gen/global_names.h"

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

struct RegionEntry {
  std::string name;
  size_t start;
  size_t end;
  std::vector<Field> whitelist;
};

std::vector<RegionEntry> log_read_regions;
std::vector<RegionEntry> log_write_regions;

constexpr GlobalVars kWhitelistReadGlobalVars[] = {
    GlobalVars::CurrentA5, GlobalVars::CurApName, GlobalVars::CurStackBase,
    GlobalVars::AppParmHandle, GlobalVars::WMgrPort, GlobalVars::Time,
    // Low-memory global constants (read-only)
    GlobalVars::MinusOne, GlobalVars::OneOne, GlobalVars::Lo3Bytes,
    // Low-memory communal scratch space (read/write)
    GlobalVars::FPState, GlobalVars::TempRect, GlobalVars::IconBitmap,
    // `TST.W HpChk` appears in a few programs and may be related to
    // MPW which reuses this location for its own purposes?
    GlobalVars::HpChk,
    // This appears directly after `CurApName` region in memory and is
    // fairly consistently read while reading `CurApName`... it seems like
    // `CurApName` may actually extend to 34 bytes and not 32?
    GlobalVars::SaveSegHandle,
    // Gets the current volume level (always 0)
    GlobalVars::SdVolume,
    // Used to check if an old 64k ROM (-1) or newer ROM (positive value)
    GlobalVars::ROM85, GlobalVars::ScreenRow, GlobalVars::DoubleTime,
    GlobalVars::MonkeyLives};

constexpr GlobalVars kWhitelistWriteGlobalVars[] = {
    GlobalVars::FPState, GlobalVars::TempRect, GlobalVars::IconBitmap,
    // Currently we _always_ load resources regardless of this flag which
    // should be fine (but does possibly cause us to run out of memory :P)
    GlobalVars::ResLoad,
    // TODO: Why would you want to write to the MemoryManager error code?
    GlobalVars::MemErr, GlobalVars::PaintWhite,
    GlobalVars::MouseOffset, GlobalVars::MouseMask,
  };

#define RETURN_IF_WHITELISTED(address, whitelist)                       \
  if (auto iter = std::find(std::begin(whitelist), std::end(whitelist), \
                            GetGlobalVar(address));                     \
      iter != std::end(whitelist)) {                                    \
    LOG(INFO) << "Access global: " << GetGlobalVarName(address);        \
    return;                                                             \
  }

bool ShouldLogAccess(const RegionEntry& entry, uint32_t address) {
  size_t relative_offset = address - entry.start;
  return std::none_of(entry.whitelist.cbegin(), entry.whitelist.cend(),
                      [&](Field field) {
                        return field.offset <= relative_offset &&
                               relative_offset < field.offset + field.size;
                      });
}

}  // namespace

uint8_t kSystemMemoryRaw[kSystemMemorySize];
core::MemoryRegion kSystemMemory(&kSystemMemoryRaw, kSystemMemorySize);

class InitializedWatcher : public core::MemoryWatcher {
  void OnWrite(size_t offset, size_t size) override {
    DebugManager::Instance().RecordWrite(offset, offset + size);
    for (int i = 0; i < size; ++i) {
      kHasInitializedMemory[offset + i] = true;
    }
  }
};

void InstallMemoryWatcher() {
  static core::MemoryWatcher* watcher = new InitializedWatcher();
  kSystemMemory.SetWatcher(watcher);
}

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

  for (const auto& entry : log_read_regions) {
    if (within_region(entry.start, entry.end)) {
      if (ShouldLogAccess(entry, address))
        LOG(FATAL) << "Read within protected region \"" << entry.name
                   << "\": 0x" << std::hex << address << " (0x"
                   << (address - entry.start) << ")";
      else
        LOG(INFO) << "Read within unprotected region \"" << entry.name
                  << "\": 0x" << std::hex << address << " (0x"
                  << (address - entry.start) << ")";
    }
  }

  // Interrupt Vector Table
  if (within_region(0, kInterruptVectorTableEnd)) {
    return;
  }

  // System Globals
  if (within_region(kSystemGlobalsLowStart, kSystemGlobalsLowEnd) ||
      within_region(kSystemGlobalsHighStart, kSystemGlobalsHighEnd)) {
    RETURN_IF_WHITELISTED(address, kWhitelistReadGlobalVars);

    LOG(FATAL) << "Read system global at 0x" << std::hex << address << ": "
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

  for (const auto& entry : log_write_regions) {
    if (within_region(entry.start, entry.end) &&
        ShouldLogAccess(entry, address)) {
      LOG(FATAL) << "Write within protected region \"" << entry.name << "\": 0x"
                 << std::hex << address << " (0x" << (address - entry.start)
                 << ") = 0x" << value;
    }
  }

  // Interrupt Vector Table
  CHECK(!within_region(0, 0x100))
      << "IVT is read-only: 0x" << std::hex << address;

  // System Globals
  if (within_region(kSystemGlobalsLowStart, kSystemGlobalsLowEnd) ||
      within_region(kSystemGlobalsHighStart, kSystemGlobalsHighEnd)) {
    RETURN_IF_WHITELISTED(address, kWhitelistWriteGlobalVars);

    LOG(FATAL) << "Write system global at 0x" << std::hex << address << ": "
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
    LOG(FATAL) << "Writing to address reserved for native "
                  "function calls: 0x"
               << std::hex << address << " = 0x" << value;
    return;
  }

  LOG(FATAL) << "Untracked write: 0x" << std::hex << address << " = 0x"
             << value;
}

std::string MemoryMapToStr() {
  std::stringstream ss;
  ss << std::hex;
  ss << "Heap: [0x" << kHeapStart << ", 0x" << kHeapEnd << "] " << "Stack: [0x"
     << kStackEnd << ", 0x" << kStackStart << "] " << "A5 World: 0x" << a5_world
     << " (+" << above_a5_size << ", -" << below_a5_size << ")";
  return ss.str();
}

void MaybeRemoveOverlappingEntry(std::vector<RegionEntry>& entries,
                                 size_t new_offset,
                                 size_t new_size) {
  auto current_iter =
      std::find_if(entries.begin(), entries.end(), [&](RegionEntry& entry) {
        return std::max(entry.start, new_offset) <
               std::min(entry.end, new_offset + new_size);
      });
  if (current_iter != entries.end()) {
    entries.erase(current_iter);
  }
}

void LogRegionAccess(size_t offset,
                     size_t length,
                     bool on_read,
                     bool on_write,
                     const std::string& region_name,
                     std::vector<Field> whitelist_fields) {
  auto region_entry = RegionEntry{.name = region_name,
                                  .start = offset,
                                  .end = offset + length,
                                  .whitelist = std::move(whitelist_fields)};

  if (on_read) {
    MaybeRemoveOverlappingEntry(log_read_regions, offset, length);
    log_read_regions.push_back(region_entry);
  }
  if (on_write) {
    MaybeRemoveOverlappingEntry(log_write_regions, offset, length);
    log_write_regions.push_back(region_entry);
  }
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

void LogStack(uint32_t stack_head) {
  LOG(INFO) << "Stack:\n"
            << MUST(memory::kSystemMemory.Create("Stack", stack_head,
                                                 kStackStart - stack_head));
}

}  // namespace debug
}  // namespace memory
}  // namespace cyder