#include "resource_file.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <cstring>
#include <iostream>
#include <map>

#include "absl/status/status.h"
#include "core/endian_helpers.h"
#include "core/logging.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "resource_types.h"

namespace rsrcloader {
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
  void* mmap_ptr = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, /*offset=*/0);
  if (mmap_ptr == MAP_FAILED) {
    return LoadFileError(path);
  }

  core::MemoryRegion base_region(mmap_ptr, size);

  auto file_header = TRY(ReadType<ResourceHeader>(base_region, /*offset=*/0));
  LOG(INFO) << "ResourceHeader: " << file_header;

  auto map_header =
      TRY(ReadType<ResourceMapHeader>(base_region, file_header.map_offset));
  LOG(INFO) << "ResourceMapHeader: " << map_header;

  core::MemoryRegion data_region = TRY(base_region.Create(
      "Data", file_header.data_offset, file_header.data_length));
  core::MemoryRegion map_region = TRY(base_region.Create(
      "Map", file_header.map_offset, file_header.map_length));
  core::MemoryRegion type_list_region =
      TRY(map_region.Create("TypeList", map_header.type_list_offset));
  core::MemoryRegion name_list_region =
      TRY(map_region.Create("NameList", map_header.name_list_offset));

  auto type_list =
      TRY(ReadType<ResourceTypeList>(type_list_region, /*offset=*/0));
  std::vector<std::unique_ptr<ResourceGroup>> resource_groups;
  for (const auto& type_item : type_list.items) {
    resource_groups.push_back(TRY(ResourceGroup::Load(
        type_list_region, name_list_region, data_region, type_item)));
  }

  return std::unique_ptr<ResourceFile>(
      new ResourceFile(std::move(resource_groups)));
}

Resource* ResourceFile::FindByTypeAndId(ResType theType, ResId theId) const {
  for (const auto& group : resource_groups_) {
    if (group->GetType() == theType) {
      return group->FindById(theId);
    }
  }
  return nullptr;
}

Resource* ResourceFile::FindByTypeAndName(ResType theType,
                                          absl::string_view theName) const {
  for (const auto& group : resource_groups_) {
    if (group->GetType() == theType) {
      return group->FindByName(theName);
    }
  }
  return nullptr;
}

ResourceGroup* ResourceFile::FindGroupByType(ResType theType) const {
  for (const auto& group : resource_groups_) {
    if (group->GetType() == theType) {
      return group.get();
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
