#pragma once

#include <cstdint>
#include <ostream>
#include <string>

#include "absl/status/statusor.h"
#include "core/memory_region.h"
#include "in_memory_types.h"
#include "resource_types.h"

namespace rsrcloader {

class Resource {
 public:
  Resource(ResId id,
           ResType type,
           uint8_t attributes,
           std::string name,
           const core::MemoryRegion& data,
           uint32_t size);

  static absl::StatusOr<std::unique_ptr<Resource>> Load(
      const ResourceTypeItem& type_item,
      const core::MemoryRegion& type_list_region,
      const core::MemoryRegion& name_list_region,
      const core::MemoryRegion& data_region,
      const ResourceEntry& entry);


  ResId GetId() const { return id_; }
  ResType GetType() const { return type_; }
  const std::string& GetName() const { return name_; }
  uint8_t GetAttributes() const { return attributes_; }
  uint32_t GetSize() const { return size_; }
  const core::MemoryRegion& GetData() const { return data_; }

 protected:
  // Disallow copy and assign:
  Resource(const Resource&) = delete;
  Resource& operator=(Resource&) = delete;

 private:
  friend std::ostream& operator<<(std::ostream&, const Resource&);

  const ResId id_;
  const ResType type_;
  const uint8_t attributes_;
  const std::string name_;
  const core::MemoryRegion data_;
  const uint32_t size_;
};

std::ostream& operator<<(std::ostream&, const Resource&);

std::string GetTypeName(ResType);

}  // namespace rsrcloader