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
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "in_memory_types.h"

namespace rsrcloader {
namespace {

absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
}

absl::StatusOr<InMemoryMapHeader> parseResourceHeader(
    const MemoryRegion& base) {
  InMemoryHeader header =
      TRY(base.Copy<InMemoryHeader>(/*offset=*/0), "Failed to parse header");
  header.data_offset = be32toh(header.data_offset);
  header.data_length = be32toh(header.data_length);
  header.map_offset = be32toh(header.map_offset);
  header.map_length = be32toh(header.map_length);

  InMemoryMapHeader map_header =
      TRY(base.Copy<InMemoryMapHeader>(header.map_offset),
          "Failed to parse map header");
  map_header.header = std::move(header);
  map_header.file_attributes = be16toh(map_header.file_attributes);
  map_header.type_list_offset = be16toh(map_header.type_list_offset);
  map_header.name_list_offset = be16toh(map_header.name_list_offset);
  map_header.type_list_count = be16toh(map_header.type_list_count);
  return std::move(map_header);
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

  MemoryRegion base_region(mmap_ptr, size);
  InMemoryMapHeader header = TRY(parseResourceHeader(base_region));

  MemoryRegion data_region =
      TRY(base_region.Create("Data", header.header.data_offset));
  MemoryRegion map_region =
      TRY(base_region.Create("Map", header.header.map_offset));
  MemoryRegion type_list_region =
      TRY(map_region.Create("TypeList", header.type_list_offset));
  MemoryRegion name_list_region =
      TRY(map_region.Create("NameList", header.name_list_offset));

  std::vector<std::unique_ptr<ResourceGroup>> resource_groups;
  for (size_t item = 0; item <= header.type_list_count; ++item) {
    resource_groups.push_back(TRY(ResourceGroup::Load(
        type_list_region, name_list_region, data_region, item)));
  }

  return std::unique_ptr<ResourceFile>(
      new ResourceFile(std::move(resource_groups)));
}

absl::Status ResourceFile::Save(const std::string& path) {
  uint32_t total_data_size = 0;

  for (const auto& group : resource_groups_) {
    for (const auto& resource : group->GetResources()) {
      total_data_size += sizeof(uint32_t) + resource->GetSize();
    }
  }

  std::vector<InMemoryTypeItem> type_list;
  std::vector<InMemoryReferenceEntry> ref_list;

  uint16_t reference_offset =
      resource_groups_.size() * sizeof(InMemoryTypeItem) + sizeof(uint16_t);
  uint32_t data_offset = 0;

  std::vector<std::string> names;
  uint16_t name_offset = 0;
  auto calculate_name_offset = [&](const std::string& name) -> uint16_t {
    if (name.empty()) {
      return 0xFFFF;
    }

    names.push_back(name);
    uint16_t current_name_offset = name_offset;
    name_offset += sizeof(uint8_t) + name.size();
    return htobe16(current_name_offset);
  };

  for (const auto& group : resource_groups_) {
    type_list.push_back(InMemoryTypeItem{.type = htobe32(group->GetType()),
                                         .count = htobe16(static_cast<uint16_t>(
                                             group->GetResources().size() - 1)),
                                         .offset = htobe16(reference_offset)});
    reference_offset +=
        group->GetResources().size() * sizeof(InMemoryReferenceEntry);
    for (const auto& resource : group->GetResources()) {
      ref_list.push_back(InMemoryReferenceEntry{
          .id = htobe16(resource->GetId()),
          .name_offset = calculate_name_offset(resource->GetName()),
          .offset = htobe32((resource->GetAttributes() << 24) |
                            (data_offset & 0x00FFFFFF)),
          .handle = 0});
      data_offset += sizeof(uint32_t) + resource->GetSize();
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

  for (const auto& group : resource_groups_) {
    for (const auto& resource : group->GetResources()) {
      uint32_t size = htobe32(resource->GetSize());
      write(&size, sizeof(uint32_t));
      write(resource->GetData().raw_ptr(), resource->GetSize());
    }
  }

  InMemoryMapHeader map_header;
  map_header.type_list_offset =
      htobe16(sizeof(InMemoryMapHeader) - sizeof(uint16_t));
  map_header.type_list_count = htobe16(type_list.size() - 1);
  map_header.name_list_offset = htobe16(total_map_size - name_offset);
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

Resource* ResourceFile::FindByTypeAndId(ResType theType, ResID theId) {
  for (const auto& group : resource_groups_) {
    if (group->GetType() == theType) {
      return group->FindById(theId);
    }
  }
  return nullptr;
}

ResourceFile::ResourceFile(
    std::vector<std::unique_ptr<ResourceGroup>> resource_groups)
    : resource_groups_(std::move(resource_groups)) {}

std::ostream& operator<<(std::ostream& out, const ResourceFile& value) {
  return out;
}

}  // namespace rsrcloader
