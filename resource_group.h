#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <vector>

#include "absl/status/statusor.h"
#include "core/memory_region.h"
#include "in_memory_types.h"
#include "resource.h"

namespace rsrcloader {

class ResourceGroup {
 public:
  ResourceGroup(InMemoryTypeItem, std::vector<std::unique_ptr<Resource>>);

  static absl::StatusOr<std::unique_ptr<ResourceGroup>> Load(
      const MemoryRegion& type_list_region,
      const MemoryRegion& name_list_region,
      const MemoryRegion& data_region,
      size_t type_item_index);

  Resource* FindById(ResID) const;

  ResType GetType() const { return type_item_.type; }

  const std::vector<std::unique_ptr<Resource>>& GetResources() const {
    return resources_;
  }

 protected:
  // Disallow copy and assign:
  ResourceGroup(const ResourceGroup&) = delete;
  ResourceGroup& operator=(ResourceGroup&) = delete;

 private:
  friend std::ostream& operator<<(std::ostream&, const ResourceGroup&);

  InMemoryTypeItem type_item_;
  std::vector<std::unique_ptr<Resource>> resources_;
};

std::ostream& operator<<(std::ostream&, const ResourceGroup&);

}  // namespace rsrcloader