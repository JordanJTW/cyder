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

absl::Status ResourceFile::Save(const std::string& path) const {
  std::vector<InMemoryTypeItem> type_item_list;
  std::vector<InMemoryReferenceEntry> reference_entry_list;
  std::vector<std::string> name_entry_list;

  // Must account for the type list count (a uint16_t) and type items:
  size_t reference_offset =
      sizeof(uint16_t) + resource_groups_.size() * sizeof(InMemoryTypeItem);

  size_t name_offset = 0, data_offset = 0;
  for (const auto& group : resource_groups_) {
    type_item_list.push_back(group->Save(reference_offset));
    for (const auto& resource : group->GetResources()) {
      reference_entry_list.push_back(
          resource->Save(name_entry_list, name_offset, data_offset));
      reference_offset += sizeof(InMemoryReferenceEntry);
    }
  }

  // The header occupies 16 bytes + 240 bytes of reserved space followed
  // by the data and map (header, type list, reference list, name list):
  const size_t file_header_size = 0x100;
  const uint32_t total_data_size = data_offset;
  const uint32_t total_map_size =
      sizeof(InMemoryMapHeader) +
      sizeof(InMemoryTypeItem) * type_item_list.size() +
      sizeof(InMemoryReferenceEntry) * reference_entry_list.size() +
      name_offset;
  const size_t total_size = file_header_size + total_data_size + total_map_size;

  InMemoryHeader header;
  header.data_offset = htobe32(file_header_size);
  header.map_offset = htobe32(file_header_size + total_data_size);
  header.data_length = htobe32(total_data_size);
  header.map_length = htobe32(total_map_size);

  InMemoryMapHeader map_header;
  map_header.type_list_offset =
      htobe16(sizeof(InMemoryMapHeader) - sizeof(uint16_t));
  map_header.type_list_count = htobe16(type_item_list.size() - 1);
  map_header.name_list_offset = htobe16(total_map_size - name_offset);

  // Begin writing out bytes of data:
  uint8_t output[total_size];
  size_t write_offset = 0;
  auto write_sequential = [&](const void* data, size_t length) {
    memcpy(output + write_offset, data, length);
    write_offset += length;
  };

  write_sequential(&header, sizeof(InMemoryHeader));
  write_offset = 0x100;

  for (const auto& group : resource_groups_) {
    for (const auto& resource : group->GetResources()) {
      uint32_t size = htobe32(resource->GetSize());
      write_sequential(&size, sizeof(uint32_t));
      write_sequential(resource->GetData().raw_ptr(), resource->GetSize());
    }
  }

  write_sequential(&map_header, sizeof(InMemoryMapHeader));
  for (const auto& item : type_item_list) {
    write_sequential(&item, sizeof(InMemoryTypeItem));
  }
  for (const auto& entry : reference_entry_list) {
    write_sequential(&entry, sizeof(InMemoryReferenceEntry));
  }
  for (const auto& name : name_entry_list) {
    uint8_t size = name.size();
    write_sequential(&size, sizeof(uint8_t));
    write_sequential(name.c_str(), size);
  }

  CHECK_EQ(write_offset, total_size);
  FILE* file = fopen(path.c_str(), "wb");
  fwrite(output, 1, total_size, file);
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
  for (const auto& group : value.resource_groups_) {
    out << *group;
  }
  return out;
}

}  // namespace rsrcloader
