// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "resource.h"

#include <cstring>
#include <iomanip>

#include "core/endian_helpers.h"
#include "core/status_helpers.h"

namespace rsrcloader {

// static
absl::StatusOr<Resource> Resource::Load(
    const core::MemoryRegion& name_list_region,
    const core::MemoryRegion& data_region,
    const ResourceEntry& entry) {
  uint32_t resource_size = TRY(data_region.Read<uint32_t>(entry.data_offset),
                               "Failed to parse resource size");

  core::MemoryRegion resource_region = TRY(data_region.Create(
      "Resource", entry.data_offset + sizeof(uint32_t), resource_size));

  std::string name;
  // If the offset is 0xFFFF then this resource has no name
  if (entry.name_offset != 0xFFFF) {
    name = TRY(ReadType<std::string>(name_list_region, entry.name_offset));
  }

  return Resource(entry, resource_region, name);
}

Resource::Resource(const ResourceEntry& entry,
                   const core::MemoryRegion& data,
                   std::string name)
    : entry_(entry), data_(data), name_(std::move(name)) {}

std::ostream& operator<<(std::ostream& out, const Resource& value) {
  out << "Resource(id: " << value.GetId();
  if (!value.GetName().empty()) {
    out << ", name: '" << value.GetName() << "'";
  }
  return out << ") is " << value.GetSize() << " bytes";
}

std::string GetTypeName(ResType theType) {
  char type_name[4];
  // The type value is actually a 4 byte string so we must reverse it
  // back to big endian for the text to appear correctly
  uint32_t reversed_type = htobe(theType);
  memcpy(type_name, &reversed_type, sizeof(uint32_t));
  return std::string(type_name, 4);
}

}  // namespace rsrcloader
