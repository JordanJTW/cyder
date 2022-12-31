// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/rsrc/resource_group.h"

#include <cstring>
#include <iomanip>

#include "core/endian_helpers.h"
#include "core/status_helpers.h"

namespace cyder {
namespace rsrc {

ResourceGroup::ResourceGroup(ResourceTypeItem type_item,
                             std::vector<Resource> resources)
    : type_item_(std::move(type_item)), resources_(std::move(resources)) {}

// static
absl::StatusOr<ResourceGroup> ResourceGroup::Load(
    const core::MemoryRegion& type_list_region,
    const core::MemoryRegion& name_list_region,
    const core::MemoryRegion& data_region,
    const ResourceTypeItem& type_item) {
  std::vector<Resource> resources;
  size_t relative_offset = 0;
  for (size_t index = 0; index <= type_item.count; ++index) {
    auto entry =
        TRY(ReadType<ResourceEntry>(type_list_region,
                                    type_item.offset + relative_offset),
            absl::StrCat("Failed to parse reference entry at ", index));
    relative_offset += entry.size();

    resources.push_back(
        TRY(Resource::Load(name_list_region, data_region, entry)));
  }

  return ResourceGroup(std::move(type_item), std::move(resources));
}

const Resource* ResourceGroup::FindById(ResId theId) const {
  for (const auto& resource : resources_) {
    if (resource.GetId() == theId) {
      return &resource;
    }
  }
  return nullptr;
}

const Resource* ResourceGroup::FindByName(absl::string_view theName) const {
  for (const auto& resource : resources_) {
    if (resource.GetName() == theName) {
      return &resource;
    }
  }
  return nullptr;
}

std::ostream& operator<<(std::ostream& out, const ResourceGroup& value) {
  out << "Group(type: '" << GetTypeName(value.GetType()) << "'):\n";
  for (const auto& resource : value.resources_) {
    out << "  + " << resource << "\n";
  }
  return out << "\n";
}

}  // namespace rsrc
}  // namespace cyder

