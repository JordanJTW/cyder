// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "emu/rsrc/resource.h"
#include "emu/rsrc/resource_group.h"

namespace cyder {
namespace rsrc {

class ResourceFile {
 public:
  // Loads a MacBinary or raw resource fork from the path given
  static absl::StatusOr<std::unique_ptr<ResourceFile>> Load(const std::string&);
  // Loads a raw resource fork from the region given
  static absl::StatusOr<std::unique_ptr<ResourceFile>> LoadRsrcFork(
      const core::MemoryRegion&);

  const Resource* FindByTypeAndId(ResType, ResId) const;
  const Resource* FindByTypeAndName(ResType, absl::string_view) const;

  const ResourceGroup* FindGroupByType(ResType theType) const;

  absl::Status Save(const std::string&);

  const std::vector<ResourceGroup>& groups() const { return resource_groups_; }

 protected:
  // Disallow copy and assign:
  ResourceFile(const ResourceFile&) = delete;
  ResourceFile& operator=(ResourceFile&) = delete;

 private:
  friend std::ostream& operator<<(std::ostream&, const ResourceFile&);

  ResourceFile(std::vector<ResourceGroup> resource_groups);

  std::vector<ResourceGroup> resource_groups_;
};

std::ostream& operator<<(std::ostream&, const ResourceFile&);

}  // namespace rsrc
}  // namespace cyder
