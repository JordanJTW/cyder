#pragma once

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "in_memory_types.h"
#include "resource.h"

namespace rsrcloader {

class ResourceFile {
 public:
  static absl::StatusOr<std::unique_ptr<ResourceFile>> Load(const std::string&);

  absl::Status Save(const std::string&);

  Resource* GetByTypeAndId(ResType, ResID);

 protected:
  // Disallow copy and assign:
  ResourceFile(const ResourceFile&) = delete;
  ResourceFile& operator=(ResourceFile&) = delete;

 private:
  ResourceFile(InMemoryMapHeader header,
               std::vector<std::unique_ptr<Resource>> resources);

  friend std::ostream& operator<<(std::ostream&, const ResourceFile&);

  const InMemoryMapHeader header_;
  std::vector<std::unique_ptr<Resource>> resources_;
};

std::ostream& operator<<(std::ostream&, const ResourceFile&);

}  // namespace rsrcloader