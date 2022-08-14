#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "core/memory_region.h"
#include "resource_types.h"
#include "resource.h"

namespace rsrcloader {

class ResourceGroup {
 public:
  ResourceGroup(ResourceTypeItem, std::vector<std::unique_ptr<Resource>>);

  static absl::StatusOr<std::unique_ptr<ResourceGroup>> Load(
      const core::MemoryRegion& type_list_region,
      const core::MemoryRegion& name_list_region,
      const core::MemoryRegion& data_region,
      const ResourceTypeItem& type_item);

  Resource* FindById(ResId) const;
  Resource* FindByName(absl::string_view) const;

  ResType GetType() const { return type_item_.type_id; }
  size_t GetSize() const { return resources_.size(); }
  size_t GetCount() const { return GetSize() - 1; }

  const std::vector<std::unique_ptr<Resource>>& GetResources() const {
    return resources_;
  }

 protected:
  // Disallow copy and assign:
  ResourceGroup(const ResourceGroup&) = delete;
  ResourceGroup& operator=(ResourceGroup&) = delete;

 private:
  friend std::ostream& operator<<(std::ostream&, const ResourceGroup&);

  ResourceTypeItem type_item_;
  std::vector<std::unique_ptr<Resource>> resources_;
};

std::ostream& operator<<(std::ostream&, const ResourceGroup&);

}  // namespace rsrcloader