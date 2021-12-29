#include "resource.h"

#include <cstring>
#include <iomanip>

#include "core/endian.h"
#include "core/status_helpers.h"

namespace rsrcloader {
namespace {

absl::StatusOr<std::string> ParseNameFromTable(const InMemoryMapHeader& header,
                                               uint32_t name_offset_in_table,
                                               const uint8_t* const base_ptr,
                                               size_t size) {
  if (name_offset_in_table == 0xFFFF) {
    return std::string{};
  }

  uint32_t offset =
      header.header.map_offset + header.name_list_offset + name_offset_in_table;
  if (size < offset + 1) {
    return absl::InternalError("Overflow reading name length");
  }

  uint8_t length = base_ptr[offset];
  if (size < offset + 1 + length) {
    return absl::InternalError("Overflow reading name value");
  }

  char str[length];
  memcpy(str, base_ptr + offset + 1, length);
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
    const uint8_t* const base_ptr,
    size_t size,
    const InMemoryMapHeader& header,
    const InMemoryTypeItem& type_item,
    size_t index) {
  auto reference_offset = [&](size_t index) {
    return header.header.map_offset + header.type_list_offset +
           type_item.offset + sizeof(InMemoryReferenceEntry) * index;
  };

  if (size < reference_offset(index)) {
    return absl::InternalError(
        absl::StrCat("Overflow reading resource at index: ", index));
  }

  InMemoryReferenceEntry entry;
  memcpy(&entry, base_ptr + reference_offset(index),
         sizeof(InMemoryReferenceEntry));
  entry.id = be16toh(entry.id);
  entry.name_offset = be16toh(entry.name_offset);

  entry.offset = be32toh(entry.offset);
  // The attributes (1 byte) and offset (3 bytes) are packed
  // together so we separate both fields here
  uint8_t attributes = (entry.offset & 0xFF000000) >> 24;
  uint32_t offset = entry.offset & 0x00FFFFFF;

  const uint8_t* const data_ptr = base_ptr + header.header.data_offset + offset;

  uint32_t resource_size;
  memcpy(&resource_size, data_ptr, sizeof(uint32_t));
  resource_size = be32toh(resource_size);

  std::string name =
      TRY(ParseNameFromTable(header, entry.name_offset, base_ptr, size));

  return absl::make_unique<Resource>(entry.id, type_item.type, attributes, name,
                                     data_ptr, size);
}

Resource::Resource(uint16_t id,
                   uint32_t type,
                   uint8_t attributes,
                   std::string name,
                   const uint8_t* const data_ptr,
                   uint32_t size)
    : id_(id),
      type_(type),
      attributes_(attributes),
      name_(std::move(name)),
      data_ptr_(data_ptr),
      size_(size) {}

std::ostream& operator<<(std::ostream& out, const Resource& value) {
  return out << "Resource '" << value.GetTypeName() << "' (" << std::setw(5)
             << value.id_ << ") is " << value.size_ << " bytes";
}

}  // namespace rsrcloader
