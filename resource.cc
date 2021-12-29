#include "resource.h"

#include <cstring>
#include <iomanip>

#include "core/endian.h"
#include "core/status_helpers.h"

namespace rsrcloader {
namespace {

absl::StatusOr<std::string> ParseNameFromTable(
    const MemoryRegion& name_list_region,
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

std::string Resource::GetTypeName() const {
  char type_name[4];
  // The type value is actually a 4 byte string so we must reverse it
  // back to big endian for the text to appear correctly
  uint32_t reversed_type = htobe32(type_);
  memcpy(type_name, &reversed_type, sizeof(uint32_t));
  return type_name;
}

// static
absl::StatusOr<std::unique_ptr<Resource>> Resource::Load(
    const InMemoryTypeItem& type_item,
    const MemoryRegion& type_list_region,
    const MemoryRegion& name_list_region,
    const MemoryRegion& data_region,
    size_t index) {
  InMemoryReferenceEntry entry =
      TRY(type_list_region.Copy<InMemoryReferenceEntry>(
              type_item.offset + sizeof(InMemoryReferenceEntry) * index),
          absl::StrCat("Failed to parse reference entry at ", index));

  entry.id = be16toh(entry.id);
  entry.offset = be32toh(entry.offset);
  entry.name_offset = be16toh(entry.name_offset);
  // The attributes (1 byte) and offset (3 bytes) are packed
  // together so we separate both fields here
  uint8_t attributes = (entry.offset & 0xFF000000) >> 24;
  uint32_t offset = entry.offset & 0x00FFFFFF;

  std::string name =
      TRY(ParseNameFromTable(name_list_region, entry.name_offset),
          "Failed to parse name from table");

  uint32_t resource_size = be32toh(
      TRY(data_region.Copy<uint32_t>(offset), "Failed to parse resource size"));

  MemoryRegion resource_region =
      TRY(data_region.Create("Resource", offset + sizeof(uint32_t)));

  return absl::make_unique<Resource>(entry.id, type_item.type, attributes, name,
                                     resource_region, resource_size);
}

Resource::Resource(uint16_t id,
                   uint32_t type,
                   uint8_t attributes,
                   std::string name,
                   const MemoryRegion& data,
                   uint32_t size)
    : id_(id),
      type_(type),
      attributes_(attributes),
      name_(std::move(name)),
      data_(data),
      size_(size) {}

std::ostream& operator<<(std::ostream& out, const Resource& value) {
  return out << "Resource '" << value.GetTypeName() << "' (" << std::setw(5)
             << value.id_ << ") is " << value.size_ << " bytes";
}

}  // namespace rsrcloader
