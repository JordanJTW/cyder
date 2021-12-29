#include "resource_group.h"

#include <cstring>
#include <iomanip>

#include "core/endian.h"
#include "core/status_helpers.h"

namespace rsrcloader {
namespace {}  // namespace

ResourceGroup::ResourceGroup(InMemoryTypeItem type_item,
                             std::vector<std::unique_ptr<Resource>> resources)
    : type_item_(std::move(type_item)), resources_(std::move(resources)) {}

// static
absl::StatusOr<std::unique_ptr<ResourceGroup>> ResourceGroup::Load(
    const MemoryRegion& type_list_region,
    const MemoryRegion& name_list_region,
    const MemoryRegion& data_region,
    size_t type_item_index) {
  // The type list begins with a uint16_t count value immediately
  // preceding the type list items so account for it here
  size_t type_item_offset =
      type_item_index * sizeof(InMemoryTypeItem) + sizeof(uint16_t);

  InMemoryTypeItem type_item =
      TRY(type_list_region.Copy<InMemoryTypeItem>(type_item_offset));

  type_item.type = be32toh(type_item.type);
  type_item.count = be16toh(type_item.count);
  type_item.offset = be16toh(type_item.offset);

  std::vector<std::unique_ptr<Resource>> resources;
  for (size_t index = 0; index <= type_item.count; ++index) {
    resources.push_back(TRY(Resource::Load(
        type_item, type_list_region, name_list_region, data_region, index)));
  }

  return std::unique_ptr<ResourceGroup>(
      new ResourceGroup(std::move(type_item), std::move(resources)));
}

Resource* ResourceGroup::FindById(ResID theId) const {
  for (const auto& resource : resources_) {
    if (resource->GetId() == theId) {
      return resource.get();
    }
  }
  return nullptr;
}

std::ostream& operator<<(std::ostream& out, const ResourceGroup& value) {
  return out;
}

}  // namespace rsrcloader
