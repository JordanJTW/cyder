// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "finder_flags.h"

namespace cyder {
namespace hfs {
namespace {

constexpr FinderFlag kAllFinderFlags[] = {
    kHasBeenInited, kHasCustomIcon, kIsStationary,
    kNameLocked,    kHasBundle,     kIsInvisible,
    kIsAlias,       kIsOnDesk,      kRequiresSwitchLaunch,
    kIsShared,      kHasNoInits,
};

}  // namespace

std::set<FinderFlag> ParseFinderFlags(uint16_t flags) {
  std::set<FinderFlag> flag_set;
  for (auto flag : kAllFinderFlags) {
    if (flag & flags) {
      flag_set.insert(flag);
    }
  }
  return flag_set;
}

std::ostream& operator<<(std::ostream& os, const FinderFlag value) {
#define ENUM_NAME(enum) \
  case enum:            \
    return os << #enum
  switch (value) {
    ENUM_NAME(kHasBeenInited);
    ENUM_NAME(kHasCustomIcon);
    ENUM_NAME(kIsStationary);
    ENUM_NAME(kNameLocked);
    ENUM_NAME(kHasBundle);
    ENUM_NAME(kIsInvisible);
    ENUM_NAME(kIsAlias);
    ENUM_NAME(kIsOnDesk);
    ENUM_NAME(kRequiresSwitchLaunch);
    ENUM_NAME(kIsShared);
    ENUM_NAME(kHasNoInits);
  }
  return os;
#undef ENUM_NAME
}

}  // namespace hfs
}  // namespace cyder
