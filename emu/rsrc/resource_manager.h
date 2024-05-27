// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "absl/strings/string_view.h"
#include "emu/memory/memory_manager.h"
#include "emu/rsrc/resource.h"
#include "emu/rsrc/resource_file.h"
#include "emu/rsrc/resource_types.tdef.h"
#include "gen/typegen/generated_types.tdef.h"

namespace cyder {

class ResourceManager {
 public:
  ResourceManager(memory::MemoryManager&,
                  rsrc::ResourceFile&,
                  rsrc::ResourceFile*);

  static ResourceManager& the();

  const rsrc::Resource* GetSegmentZero() const;

  Handle GetResource(ResType, ResId);
  Handle GetResourseByName(ResType, absl::string_view);

 private:
  memory::MemoryManager& memory_manager_;
  rsrc::ResourceFile& resource_file_;
  rsrc::ResourceFile* system_file_;

  std::map<std::string, uint32_t> resource_to_handle_;
};

}  // namespace cyder