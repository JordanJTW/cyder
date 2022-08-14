#include "resource.h"

#include <cstring>
#include <iomanip>

#include "core/endian_helpers.h"
#include "core/status_helpers.h"

namespace rsrcloader {
namespace {

absl::StatusOr<std::string> ParseNameFromTable(
    const core::MemoryRegion& name_list_region,
    uint16_t name_offset) {
  if (name_offset == 0xFFFF) {
    return std::string{};
  }

  char str[255];
  uint8_t length = TRY(name_list_region.Copy<uint8_t>(name_offset));
  RETURN_IF_ERROR(name_list_region.Copy(&str, name_offset + 1, length));
  return std::string(str, length);
}

}  // namespace

// static
absl::StatusOr<std::unique_ptr<Resource>> Resource::Load(
    const ResourceTypeItem& type_item,
    const core::MemoryRegion& type_list_region,
    const core::MemoryRegion& name_list_region,
    const core::MemoryRegion& data_region,
    const ResourceEntry& entry) {
  // The attributes (1 byte) and offset (3 bytes) are packed
  // together so we separate both fields here
  uint8_t attributes = (entry.offset & 0xFF000000) >> 24;
  uint32_t offset = entry.offset & 0x00FFFFFF;

  std::string name =
      TRY(ParseNameFromTable(name_list_region, entry.name_offset),
          "Failed to parse name from table");

  uint32_t resource_size = be32toh(
      TRY(data_region.Copy<uint32_t>(offset), "Failed to parse resource size"));

  core::MemoryRegion resource_region = TRY(
      data_region.Create("Resource", offset + sizeof(uint32_t), resource_size));

  return absl::make_unique<Resource>(entry.id, type_item.type_id, attributes,
                                     name, resource_region, resource_size);
}

Resource::Resource(uint16_t id,
                   uint32_t type,
                   uint8_t attributes,
                   std::string name,
                   const core::MemoryRegion& data,
                   uint32_t size)
    : id_(id),
      type_(type),
      attributes_(attributes),
      name_(std::move(name)),
      data_(data),
      size_(size) {}

std::ostream& operator<<(std::ostream& out, const Resource& value) {
  out << "Resource(id: " << value.GetId();
  if (!value.GetName().empty()) {
    out << ", name: '" << value.GetName() << "'";
  }
  out << ") is " << value.GetSize() << " bytes\n";
  return out << value.GetData();
}

std::string GetTypeName(ResType theType) {
  char type_name[4];
  // The type value is actually a 4 byte string so we must reverse it
  // back to big endian for the text to appear correctly
  uint32_t reversed_type = htobe32(theType);
  memcpy(type_name, &reversed_type, sizeof(uint32_t));
  return std::string(type_name, 4);
}

}  // namespace rsrcloader
