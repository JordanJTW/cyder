#include "rsrcloader.h"

#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstring>
#include <iomanip>
#include <iostream>

#include "absl/status/status.h"
#include "in_memory_types.h"
#include "status_macros.h"

namespace rsrcloader {
namespace {

absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
}

absl::StatusOr<InMemoryMapHeader> parseResourceHeader(const uint8_t* const data,
                                                      size_t size) {
  if (size < sizeof(InMemoryHeader)) {
    return absl::InternalError("Overflow reading header");
  }

  InMemoryHeader header;
  std::memcpy(&header, data, sizeof(InMemoryHeader));
  header.data_offset = be32toh(header.data_offset);
  header.data_length = be32toh(header.data_length);
  header.map_offset = be32toh(header.map_offset);
  header.map_length = be32toh(header.map_length);

  if (size < header.map_offset + sizeof(InMemoryMapHeader)) {
    return absl::InternalError("Overflow reading map header");
  }

  InMemoryMapHeader map_header;
  std::memcpy(&map_header, data + header.map_offset, sizeof(InMemoryMapHeader));
  map_header.header = std::move(header);
  map_header.file_attributes = be16toh(map_header.file_attributes);
  map_header.type_list_offset = be16toh(map_header.type_list_offset);
  map_header.name_list_offset = be16toh(map_header.name_list_offset);
  map_header.type_list_count = be16toh(map_header.type_list_count);
  return std::move(map_header);
}

absl::StatusOr<std::vector<std::unique_ptr<Resource>>> parseResources(
    const InMemoryMapHeader& header,
    const uint8_t* const base_ptr,
    size_t size) {
  uint16_t absolute_type_list_offset =
      header.header.map_offset + header.type_list_offset;

  auto type_item_offset = [&](size_t index) {
    // The type list begins with a uint16_t count value immediately
    // preceding the type list items so account for it here
    return absolute_type_list_offset + sizeof(InMemoryTypeItem) * index +
           sizeof(uint16_t);
  };

  if (size < type_item_offset(header.type_list_count + 1)) {
    return absl::InternalError("Overflow reading type list");
  }

  std::vector<std::unique_ptr<Resource>> resources;
  for (size_t item = 0; item <= header.type_list_count; ++item) {
    InMemoryTypeItem type_item;
    memcpy(&type_item, base_ptr + type_item_offset(item),
           sizeof(InMemoryTypeItem));
    type_item.type = be32toh(type_item.type);
    type_item.count = be16toh(type_item.count);
    type_item.offset = be16toh(type_item.offset);

    auto reference_offset = [&](size_t index) {
      return absolute_type_list_offset + type_item.offset +
             sizeof(InMemoryReferenceEntry) * index;
    };

    if (size < reference_offset(type_item.count + 1)) {
      return absl::InternalError("Overflow reading reference(s)");
    }

    for (size_t index = 0; index <= type_item.count; ++index) {
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

      const uint8_t* const data_ptr =
          base_ptr + header.header.data_offset + offset;

      uint32_t resource_size;
      memcpy(&resource_size, data_ptr, sizeof(uint32_t));
      resource_size = be32toh(resource_size);

      resources.push_back(absl::make_unique<Resource>(
          entry.id, type_item.type, attributes, data_ptr, resource_size));
    }
  }
  return resources;
}

}  // namespace

// static
absl::StatusOr<std::unique_ptr<ResourceFile>> ResourceFile::Load(
    const std::string& path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return LoadFileError(path);
  }

  struct stat status;
  if (fstat(fd, &status) < 0) {
    return LoadFileError(path);
  }

  size_t size = status.st_size;
  void* mmap_ptr = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, /*offset=*/0);
  if (mmap_ptr == MAP_FAILED) {
    return LoadFileError(path);
  }

  const uint8_t* const data = reinterpret_cast<uint8_t*>(mmap_ptr);

  ASSIGN_OR_RETURN(auto header, parseResourceHeader(data, size));
  ASSIGN_OR_RETURN(auto resources, parseResources(header, data, size));

  return std::unique_ptr<ResourceFile>(
      new ResourceFile(std::move(header), std::move(resources)));
}

std::string Resource::GetTypeName() const {
  char type_name[4];
  // The type value is actually a 4 byte string so we must reverse it
  // back to big endian for the text to appear correctly
  uint32_t reversed_type = htobe32(type_);
  memcpy(type_name, &reversed_type, sizeof(uint32_t));
  return type_name;
}

ResourceFile::ResourceFile(InMemoryMapHeader header,
                           std::vector<std::unique_ptr<Resource>> resources)
    : header_(std::move(header)), resources_(std::move(resources)) {}

Resource::Resource(uint16_t id,
                   uint32_t type,
                   uint8_t attributes,
                   const uint8_t* const data_ptr,
                   uint32_t size)
    : id_(id),
      type_(type),
      attributes_(attributes),
      data_ptr_(data_ptr),
      size_(size) {}

std::ostream& operator<<(std::ostream& out, const Resource& value) {
  return out << "Resource '" << value.GetTypeName() << "' (" << std::setw(5)
             << value.id_ << ") is " << value.size_ << " bytes";
}

std::ostream& operator<<(std::ostream& out, const ResourceFile& value) {
  for (const auto& entry : value.resources_) {
    out << *entry << "\n";
  }
  return out;
}

}  // namespace rsrcloader