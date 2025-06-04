// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <ostream>
#include <set>

namespace cyder {
namespace hfs {

// Based on a description of HFS flag meanings from MacBinary III spec.
// Link:
// https://github.com/mietek/theunarchiver/wiki/MacBinarySpecs#macbinary-iii
enum FinderFlag {
  // High Byte:
  kHasBeenInited = 1 << 0,
  // kReserved = 1 << 1,
  kHasCustomIcon = 1 << 2,
  kIsStationary = 1 << 3,
  kNameLocked = 1 << 4,
  kHasBundle = 1 << 5,
  kIsInvisible = 1 << 6,
  kIsAlias = 1 << 7,
  // Low Byte:
  kIsOnDesk = 1 << 8,
  // color 9-11
  // kReserved = 1 << 12
  kRequiresSwitchLaunch = 1 << 13,
  kIsShared = 1 << 14,
  kHasNoInits = 1 << 15
};

// Parse u16 packed Finder flags to `FinderFlags`:
std::set<FinderFlag> ParseFinderFlags(uint16_t flags);

std::ostream& operator<<(std::ostream& os, const FinderFlag value);

}  // namespace hfs
}  // namespace cyder
