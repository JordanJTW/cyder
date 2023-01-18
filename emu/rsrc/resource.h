// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/status/statusor.h"
#include "core/memory_region.h"
#include "emu/rsrc/resource_types.tdef.h"

namespace cyder {
namespace rsrc {

class Resource {
 public:
  Resource(const ResourceEntry& entry,
           const core::MemoryRegion& data,
           std::string name);

  static absl::StatusOr<Resource> Load(
      const core::MemoryRegion& name_list_region,
      const core::MemoryRegion& data_region,
      const ResourceEntry& entry);

  ResId GetId() const { return entry_.id; }
  const std::string& GetName() const { return name_; }
  uint8_t GetAttributes() const { return entry_.attributes; }
  uint32_t GetSize() const { return data_.size(); }
  const core::MemoryRegion& GetData() const { return data_; }

 private:
  friend std::ostream& operator<<(std::ostream&, const Resource&);

  const ResourceEntry entry_;
  const core::MemoryRegion data_;
  const std::string name_;
};

std::ostream& operator<<(std::ostream&, const Resource&);

}  // namespace rsrc
}  // namespace cyder