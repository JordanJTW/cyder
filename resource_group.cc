#include "resource_group.h"

#include <cstring>
#include <iomanip>

#include "core/endian_helpers.h"
#include "core/status_helpers.h"

namespace rsrcloader {
namespace {}  // namespace

ResourceGroup::ResourceGroup(ResourceTypeItem type_item,
                             std::vector<std::unique_ptr<Resource>> resources)
    : type_item_(std::move(type_item)), resources_(std::move(resources)) {}

// static
absl::StatusOr<std::unique_ptr<ResourceGroup>> ResourceGroup::Load(
    const core::MemoryRegion& type_list_region,
    const core::MemoryRegion& name_list_region,
    const core::MemoryRegion& data_region,
    const ResourceTypeItem& type_item) {
  std::vector<std::unique_ptr<Resource>> resources;
  size_t relative_offset = 0;
  for (size_t index = 0; index <= type_item.count; ++index) {
    auto entry =
        TRY(ReadType<ResourceEntry>(type_list_region,
                                    type_item.offset + relative_offset),
            absl::StrCat("Failed to parse reference entry at ", index));
    relative_offset += entry.size();

    resources.push_back(TRY(Resource::Load(
        type_item, type_list_region, name_list_region, data_region, entry)));
  }

  return std::unique_ptr<ResourceGroup>(
      new ResourceGroup(std::move(type_item), std::move(resources)));
}

InMemoryTypeItem ResourceGroup::Save(size_t reference_offset) const {
  InMemoryTypeItem type_item;
  type_item.type = htobe32(GetType());
  type_item.count = htobe16(static_cast<uint16_t>(GetCount()));
  type_item.offset = htobe16(reference_offset);
  return type_item;
}

Resource* ResourceGroup::FindById(ResId theId) const {
  for (const auto& resource : resources_) {
    if (resource->GetId() == theId) {
      return resource.get();
    }
  }
  return nullptr;
}

Resource* ResourceGroup::FindByName(absl::string_view theName) const {
  for (const auto& resource : resources_) {
    if (resource->GetName() == theName) {
      return resource.get();
    }
  }
  return nullptr;
}

std::ostream& operator<<(std::ostream& out, const ResourceGroup& value) {
  out << "Group(type: '" << GetTypeName(value.GetType()) << "'):\n";
  for (const auto& resource : value.resources_) {
    out << "  + " << *resource << "\n";
  }
  return out << "\n";
}

}  // namespace rsrcloader
