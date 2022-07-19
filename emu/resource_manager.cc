#include "resource_manager.h"

namespace rsrcloader {
namespace {

std::string GetUniqueId(ResType theType, ResId theId) {
  return absl::StrCat("Resource[", GetTypeName(theType), ":", theId, "]");
}

}  // namespace

ResourceManager::ResourceManager(MemoryManager& memory_manager,
                                 ResourceFile& resource_file)
    : memory_manager_(memory_manager), resource_file_(resource_file) {}

Resource* ResourceManager::GetSegmentZero() const {
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
  CHECK(resource) << "Resource not found";

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

}  // namespace rsrcloader