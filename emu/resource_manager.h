#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "absl/strings/string_view.h"
#include "emu/memory/memory_manager.h"
#include "generated_types.h"
#include "resource.h"
#include "resource_file.h"
#include "resource_types.h"

namespace cyder {

class ResourceManager {
 public:
  ResourceManager(memory::MemoryManager&, rsrcloader::ResourceFile&);

  rsrcloader::Resource* GetSegmentZero() const;

  Handle GetResource(ResType, ResId);
  Handle GetResourseByName(ResType, absl::string_view);

 private:
  memory::MemoryManager& memory_manager_;
  rsrcloader::ResourceFile& resource_file_;

  std::map<std::string, uint32_t> resource_to_handle_;
};

}  // namespace cyder