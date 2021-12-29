#include "resource_file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstring>
#include <iostream>
#include <map>

#include "absl/status/status.h"
#include "core/endian.h"
#include "core/logging.h"
#include "core/status_helpers.h"
#include "in_memory_types.h"

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
  auto type_item_offset = [&](size_t index) {
    // The type list begins with a uint16_t count value immediately
    // preceding the type list items so account for it here
    return header.header.map_offset + header.type_list_offset +
           sizeof(InMemoryTypeItem) * index + sizeof(uint16_t);
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

    for (size_t index = 0; index <= type_item.count; ++index) {
      resources.push_back(
          TRY(Resource::Load(base_ptr, size, header, type_item, index)));
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

  InMemoryMapHeader header = TRY(parseResourceHeader(data, size));
  auto resources = TRY(parseResources(header, data, size));

  return std::unique_ptr<ResourceFile>(
      new ResourceFile(std::move(header), std::move(resources)));
}

absl::Status ResourceFile::Save(const std::string& path) {
  // std::sort(resources_.begin(), resources_.end(),
  //           [](const std::unique_ptr<Resource>& first,
  //              const std::unique_ptr<Resource>& second) {
  //             return first->GetType() == second->GetType()
  //                        ? first->GetId() < second->GetId()
  //                        : first->GetType() < second->GetType();
  //           });

  std::map<ResType, std::vector<const Resource*>> resourceMap;
  uint32_t total_data_size = 0;

  std::vector<ResType> resource_types;
  ResType last_type = 0;
  for (const auto& resource : resources_) {
    if (last_type != resource->GetType()) {
      resource_types.push_back(resource->GetType());
      last_type = resource->GetType();
    }
    resourceMap[resource->GetType()].push_back(resource.get());
    total_data_size += sizeof(uint32_t) + resource->GetSize();
  }

  std::vector<InMemoryTypeItem> type_list;
  std::vector<InMemoryReferenceEntry> ref_list;

  uint16_t reference_offset =
      resourceMap.size() * sizeof(InMemoryTypeItem) + sizeof(uint16_t);
  uint32_t data_offset = 0;
  std::vector<std::string> names;
  uint16_t name_offset = 0;
  for (const auto& type : resource_types) {
    const std::vector<const Resource*>& resources = resourceMap[type];
    type_list.push_back(InMemoryTypeItem{
        .type = htobe32(type),
        .count = htobe16(static_cast<uint16_t>(resources.size() - 1)),
        .offset = htobe16(reference_offset)});
    reference_offset += resources.size() * sizeof(InMemoryReferenceEntry);
    for (const auto* resource : resources) {
      ref_list.push_back(InMemoryReferenceEntry{
          .id = htobe16(resource->GetId()),
          .name_offset = resource->GetName().empty() ? (uint16_t)0xFFFF
                                                     : htobe16(name_offset),
          .offset = htobe32((resource->GetAttributes() << 24) |
                            (data_offset & 0x00FFFFFF)),
          .handle = 0});
      data_offset += sizeof(uint32_t) + resource->GetSize();
      if (!resource->GetName().empty()) {
        const auto& name = resource->GetName();
        names.push_back(name);
        name_offset += sizeof(uint8_t) + name.size();
      }
    }
  }

  uint32_t total_map_size =
      sizeof(InMemoryMapHeader) + sizeof(InMemoryTypeItem) * type_list.size() +
      sizeof(InMemoryReferenceEntry) * ref_list.size() + name_offset;

  size_t output_length = 0x100 + total_data_size + total_map_size;
  uint8_t output[output_length];

  size_t write_offset = 0;
  auto write = [&](const void* data, size_t length) {
    memcpy(output + write_offset, data, length);
    write_offset += length;
  };

  InMemoryHeader header = {.data_offset = htobe32(0x100),
                           .map_offset = htobe32(0x100 + total_data_size),
                           .data_length = htobe32(total_data_size),
                           .map_length = htobe32(total_map_size)};
  write(&header, sizeof(InMemoryHeader));
  write_offset = 0x100;

  for (const auto& type : resource_types) {
    for (const auto* resource : resourceMap[type]) {
      uint32_t size = htobe32(resource->GetSize());
      write(&size, sizeof(uint32_t));
      write(resource->GetData(), resource->GetSize());
    }
  }

  InMemoryMapHeader map_header;
  map_header.type_list_offset =
      htobe16(sizeof(InMemoryMapHeader) - sizeof(uint16_t));
  map_header.type_list_count = htobe16(type_list.size() - 1);
  map_header.name_list_offset = htobe16(
      sizeof(InMemoryMapHeader) + sizeof(InMemoryTypeItem) * type_list.size() +
      sizeof(InMemoryReferenceEntry) * ref_list.size());
  write(&map_header, sizeof(InMemoryMapHeader));

  for (const auto& item : type_list) {
    write(&item, sizeof(InMemoryTypeItem));
  }
  for (const auto& entry : ref_list) {
    write(&entry, sizeof(InMemoryReferenceEntry));
  }

  for (const auto& name : names) {
    uint8_t size = name.size();
    write(&size, sizeof(uint8_t));
    write(name.c_str(), size);
  }

  CHECK_EQ(write_offset, output_length);

  FILE* file = fopen(path.c_str(), "wb");
  fwrite(output, 1, output_length, file);
  fclose(file);

  return absl::OkStatus();
}

Resource* ResourceFile::GetByTypeAndId(ResType theType, ResID theId) {
  for (const auto& resource : resources_) {
    if (resource->GetType() == theType && resource->GetId() == theId) {
      return resource.get();
    }
  }
  return nullptr;
}

ResourceFile::ResourceFile(InMemoryMapHeader header,
                           std::vector<std::unique_ptr<Resource>> resources)
    : header_(std::move(header)), resources_(std::move(resources)) {}

std::ostream& operator<<(std::ostream& out, const ResourceFile& value) {
  for (const auto& entry : value.resources_) {
    out << *entry << "\n";
  }
  return out;
}

}  // namespace rsrcloader