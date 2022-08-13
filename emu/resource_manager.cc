#include "resource_manager.h"

#include "emu/memory/memory_map.h"
#include "global_names.h"

namespace cyder {
namespace {

using rsrcloader::GetTypeName;
using rsrcloader::Resource;

std::string GetUniqueId(ResType theType, ResId theId) {
  return absl::StrCat("Resource[", GetTypeName(theType), ":", theId, "]");
}

}  // namespace

ResourceManager::ResourceManager(memory::MemoryManager& memory_manager,
                                 rsrcloader::ResourceFile& resource_file)
    : memory_manager_(memory_manager), resource_file_(resource_file) {}

rsrcloader::Resource* ResourceManager::GetSegmentZero() const {
  return resource_file_.FindByTypeAndId('CODE', 0);
}

Handle ResourceManager::GetResource(ResType theType, ResId theId) {
  const std::string unique_id = GetUniqueId(theType, theId);

  const auto& cached_handle_pair = resource_to_handle_.find(unique_id);
  if (cached_handle_pair != resource_to_handle_.cend()) {
    LOG(INFO) << "Returning cached handle for " << unique_id;
    return cached_handle_pair->second;
  }

  const Resource* const resource =
      resource_file_.FindByTypeAndId(theType, theId);
  // FIXME: Set ResError in D0 and call ResErrorProc
  // http://0.0.0.0:8000/docs/mac/MoreToolbox/MoreToolbox-35.html#MARKER-9-220
  CHECK(resource) << "Resource not found";

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

}  // namespace cyder