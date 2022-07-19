#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "absl/strings/string_view.h"
#include "memory_manager.h"
#include "resource.h"
#include "resource_file.h"
#include "system_types.h"

namespace rsrcloader {

class ResourceManager {
 public:
  ResourceManager(MemoryManager&, ResourceFile&);

  Resource* GetSegmentZero() const;

  Handle GetResource(ResType, ResId);
  Handle GetResourseByName(ResType, absl::string_view);

 private:
  MemoryManager& memory_manager_;
  ResourceFile& resource_file_;

  std::map<std::string, uint32_t> resource_to_handle_;
};

}  // namespace rsrcloader