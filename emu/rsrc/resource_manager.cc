// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/rsrc/resource_manager.h"

#include "absl/strings/str_cat.h"
#include "emu/memory/memory_map.h"
#include "gen/global_names.h"
#include "gen/typegen/typegen_prelude.h"

namespace cyder {
namespace {

using ::cyder::rsrc::Resource;

std::string GetUniqueId(ResType theType, ResId theId) {
  return absl::StrCat("Resource[", OSTypeName(theType), ":", theId, "]");
}

static ResourceManager* s_instance;

}  // namespace

ResourceManager::ResourceManager(memory::MemoryManager& memory_manager,
                                 rsrc::ResourceFile& resource_file,
                                 rsrc::ResourceFile* system_file)
    : memory_manager_(memory_manager),
      resource_file_(resource_file),
      system_file_(system_file) {
  s_instance = this;
}

// static
ResourceManager& ResourceManager::the() {
  return *s_instance;
}

const Resource* ResourceManager::GetSegmentZero() const {
  return resource_file_.FindByTypeAndId('CODE', 0);
}

Handle ResourceManager::GetResource(ResType theType, ResId theId) {
  const std::string unique_id = GetUniqueId(theType, theId);

  const auto& cached_handle_pair = resource_to_handle_.find(unique_id);
  if (cached_handle_pair != resource_to_handle_.cend()) {
    return cached_handle_pair->second;
  }

  const Resource* resource = resource_file_.FindByTypeAndId(theType, theId);

  // If a System file was provided at start-up fallback to looking there.
  // This falling back search behavior mirrors MacOS.
  if (resource == nullptr && system_file_) {
    resource = system_file_->FindByTypeAndId(theType, theId);
  }

  // FIXME: Set ResError in D0 and call ResErrorProc
  // http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-35.html#MARKER-9-220
  CHECK(resource) << "Resource not found: " << unique_id;

  Handle handle =
      memory_manager_.AllocateHandleForRegion(resource->GetData(), unique_id);
  resource_to_handle_[unique_id] = handle;
  return handle;
}

Handle ResourceManager::GetResourseByName(ResType theType,
                                          absl::string_view theName) {
  const Resource* const resource =
      resource_file_.FindByTypeAndName(theType, theName);
  // FIXME: Set ResError in D0 and call ResErrorProc
  // http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-35.html#MARKER-9-220
  if (resource == nullptr) {
    auto status =
        memory::kSystemMemory.Write<int16_t>(GlobalVars::ResErr, -192);
    CHECK(status.ok()) << std::move(status).message();
    return 0;
  }

  const std::string unique_id = GetUniqueId(theType, resource->GetId());

  const auto& cached_handle_pair = resource_to_handle_.find(unique_id);
  if (cached_handle_pair != resource_to_handle_.cend()) {
    LOG(INFO) << "Returning cached handle for " << unique_id;
    return cached_handle_pair->second;
  }

  Handle handle =
      memory_manager_.AllocateHandleForRegion(resource->GetData(), unique_id);
  resource_to_handle_[unique_id] = handle;
  return handle;
}

std::vector<std::pair<ResId, std::string>> ResourceManager::GetIdsForType(
    ResType theType) {
  std::vector<std::pair<ResId, std::string>> id_and_names;

  for (rsrc::ResourceFile* file : {&resource_file_, system_file_}) {
    if (file == nullptr)
      continue;

    const rsrc::ResourceGroup* type_group = file->FindGroupByType(theType);
    if (type_group == nullptr)
      continue;

    for (auto& resource : type_group->GetResources())
      id_and_names.emplace_back() = {resource.GetId(), resource.GetName()};
  }
  return id_and_names;
}

}  // namespace cyder