// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/rsrc/resource_file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

#include "absl/status/status.h"
#include "core/endian_helpers.h"
#include "core/logging.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "emu/rsrc/macbinary_helpers.h"
#include "emu/rsrc/resource_types.tdef.h"

namespace cyder {
namespace rsrc {
namespace {

absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
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
  // FIXME: We should not be leaking this memory :(
  void* mmap_ptr = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, /*offset=*/0);
  if (mmap_ptr == MAP_FAILED) {
    return LoadFileError(path);
  }

  core::MemoryRegion base_region(mmap_ptr, size);

  // Try to load as a MacBinary II file before falling back to raw rsrc_fork
  auto macbinary_header = TRY(ReadType<MacBinaryHeader>(base_region));
  if (macbinary_header.is_valid) {
    return LoadRsrcFork(TRY(base_region.Create(
        "rsrc", MacBinaryHeader::fixed_size, macbinary_header.rsrc_length)));
  }
  return LoadRsrcFork(base_region);
}

// static
absl::StatusOr<std::unique_ptr<ResourceFile>> ResourceFile::LoadRsrcFork(
    const core::MemoryRegion& region) {
  auto file_header = TRY(ReadType<ResourceHeader>(region));
  LOG(INFO) << "ResourceHeader: " << file_header;

  auto map_header =
      TRY(ReadType<ResourceMapHeader>(region, file_header.map_offset));
  LOG(INFO) << "ResourceMapHeader: " << map_header;

  core::MemoryRegion data_region = TRY(
      region.Create("Data", file_header.data_offset, file_header.data_length));
  core::MemoryRegion map_region =
      TRY(region.Create("Map", file_header.map_offset, file_header.map_length));
  core::MemoryRegion type_list_region =
      TRY(map_region.Create("TypeList", map_header.type_list_offset));
  core::MemoryRegion name_list_region =
      TRY(map_region.Create("NameList", map_header.name_list_offset));

  core::MemoryReader type_list_reader(type_list_region);
  auto type_list_count = TRY(type_list_reader.Next<uint16_t>());

  std::vector<ResourceGroup> resource_groups;
  for (int i = 0; i <= type_list_count; ++i) {
    auto type_item = TRY(type_list_reader.NextType<ResourceTypeItem>());
    const ResourceGroup group = TRY(ResourceGroup::Load(
        type_list_region, name_list_region, data_region, std::move(type_item)));
    resource_groups.push_back(std::move(group));
  }

  return std::unique_ptr<ResourceFile>(
      new ResourceFile(std::move(resource_groups)));
}

absl::Status ResourceFile::Save(const std::string& path) {
  std::vector<ResourceEntry> resource_entries;
  std::vector<core::MemoryRegion> resource_data;
  std::vector<std::string> resource_names;

  // Entries start after all of the `ResourceTypeItem`s:
  // FIXME: typegen could have a static size function which takes count?
  uint16_t entry_offset =
      sizeof(uint16_t) + ResourceTypeItem::fixed_size * resource_groups_.size();
  uint32_t data_offset = 0;
  uint16_t name_offset = 0;

  std::vector<ResourceTypeItem> type_items;
  for (const auto& group : resource_groups_) {
    CHECK_LT(group.GetCount(), 0xFFFF)
        << "more than maximum allowed number of resources per group";
    type_items.push_back(
        {.type_id = group.GetType(),
         .count = static_cast<uint16_t>(group.GetCount() & 0xFFFF),
         .offset = entry_offset});

    for (const auto& resource : group.GetResources()) {
      ResourceEntry resource_entry;
      resource_entry.id = resource.GetId();
      resource_entry.attributes = resource.GetAttributes();
      resource_entry.data_offset = data_offset;

      entry_offset += resource_entry.size();
      data_offset += resource.GetSize() + sizeof(uint32_t);
      resource_data.push_back(resource.GetData());

      if (!resource.GetName().empty()) {
        resource_entry.name_offset = name_offset;
        name_offset += resource.GetName().size() + sizeof(uint8_t);
        resource_names.push_back(resource.GetName());
      } else {
        resource_entry.name_offset = 0xFFFF;
      }

      resource_entries.push_back(resource_entry);
    }
  }

  ResourceHeader file_header;
  file_header.data_offset = 0x100;
  file_header.data_length = data_offset;
  file_header.map_offset = file_header.data_offset + file_header.data_length;
  file_header.map_length =
      ResourceMapHeader::fixed_size + entry_offset + name_offset;

  ResourceMapHeader map_header;
  map_header.type_list_offset = ResourceMapHeader::fixed_size;
  map_header.name_list_offset = map_header.type_list_offset + entry_offset;
  map_header.file_header = file_header;

  // Assume that the Resouce Map is the last thing in the file:
  size_t total_size = file_header.map_offset + file_header.map_length;

  char raw_data[total_size];
  core::MemoryRegion data(raw_data, total_size);
  size_t offset = 0;

  RETURN_IF_ERROR(WriteType<ResourceHeader>(file_header, data, offset));
  offset = 0x100;

  for (const auto& entry : resource_data) {
    // FIXME: typegen the data entries? This is quite ugly...
    RETURN_IF_ERROR(data.Write(offset, static_cast<uint32_t>(entry.size())));
    RETURN_IF_ERROR(data.WriteRaw(entry.raw_ptr(), offset + sizeof(uint32_t),
                                  entry.size()));
    offset += sizeof(uint32_t) + entry.size();
  }

  RETURN_IF_ERROR(WriteType<ResourceMapHeader>(map_header, data, offset));
  offset += map_header.size();

  RETURN_IF_ERROR(data.Write<uint16_t>(offset, type_items.size() - 1));
  offset += sizeof(uint16_t);
  for (const auto& item : type_items) {
    RETURN_IF_ERROR(WriteType<ResourceTypeItem>(item, data, offset));
    offset += ResourceTypeItem::fixed_size;
  }

  for (const auto& entry : resource_entries) {
    RETURN_IF_ERROR(WriteType<ResourceEntry>(entry, data, offset));
    offset += entry.size();
  }

  for (const auto& name : resource_names) {
    RETURN_IF_ERROR(WriteType<std::string>(name, data, offset));
    offset += sizeof(uint8_t) + name.size();
  }

  std::ofstream file(path, std::ios::binary);
  file.write(raw_data, total_size);
  return absl::OkStatus();
}

const Resource* ResourceFile::FindByTypeAndId(ResType theType,
                                              ResId theId) const {
  for (const auto& group : resource_groups_) {
    if (group.GetType() == theType) {
      return group.FindById(theId);
    }
  }
  return nullptr;
}

const Resource* ResourceFile::FindByTypeAndName(
    ResType theType,
    absl::string_view theName) const {
  for (const auto& group : resource_groups_) {
    if (group.GetType() == theType) {
      return group.FindByName(theName);
    }
  }
  return nullptr;
}

const ResourceGroup* ResourceFile::FindGroupByType(ResType theType) const {
  for (const auto& group : resource_groups_) {
    if (group.GetType() == theType) {
      return &group;
    }
  }
  return nullptr;
}

ResourceFile::ResourceFile(std::vector<ResourceGroup> resource_groups)
    : resource_groups_(std::move(resource_groups)) {}

std::ostream& operator<<(std::ostream& out, const ResourceFile& value) {
  for (const auto& group : value.resource_groups_) {
    out << group;
  }
  return out;
}

}  // namespace rsrc
}  // namespace cyder
