#pragma once

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "in_memory_types.h"

namespace rsrcloader {

class Resource {
 public:
  Resource(uint16_t id,
           uint32_t type,
           uint8_t attributes,
           const uint8_t* const data_ptr,
           uint32_t size);

  std::string GetTypeName() const;

 protected:
  // Disallow copy and assign:
  Resource(const Resource&) = delete;
  Resource& operator=(Resource&) = delete;

 private:
  friend std::ostream& operator<<(std::ostream&, const Resource&);

  const uint16_t id_;
  const uint32_t type_;
  const uint8_t attributes_;
  const uint8_t* const data_ptr_;
  const uint32_t size_;
};

class ResourceFile {
 public:
  static absl::StatusOr<std::unique_ptr<ResourceFile>> Load(const std::string&);

 protected:
  // Disallow copy and assign:
  ResourceFile(const ResourceFile&) = delete;
  ResourceFile& operator=(ResourceFile&) = delete;

 private:
  ResourceFile(InMemoryMapHeader header,
               std::vector<std::unique_ptr<Resource>> resources);

  friend std::ostream& operator<<(std::ostream&, const ResourceFile&);

  const InMemoryMapHeader header_;
  const std::vector<std::unique_ptr<Resource>> resources_;
};

const std::ostream& operator<<(const std::ostream&, const Resource&);
const std::ostream& operator<<(const std::ostream&, const ResourceFile&);

}  // namespace rsrcloader