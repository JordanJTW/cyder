#pragma once

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "in_memory_types.h"
#include "resource.h"
#include "resource_group.h"

namespace rsrcloader {

class ResourceFile {
 public:
  static absl::StatusOr<std::unique_ptr<ResourceFile>> Load(const std::string&);

  absl::Status Save(const std::string&);

  Resource* FindByTypeAndId(ResType, ResID);

 protected:
  // Disallow copy and assign:
  ResourceFile(const ResourceFile&) = delete;
  ResourceFile& operator=(ResourceFile&) = delete;

 private:
  ResourceFile(std::vector<std::unique_ptr<ResourceGroup>> resource_groups);

  friend std::ostream& operator<<(std::ostream&, const ResourceFile&);

  std::vector<std::unique_ptr<ResourceGroup>> resource_groups_;
};

std::ostream& operator<<(std::ostream&, const ResourceFile&);

}  // namespace rsrcloader