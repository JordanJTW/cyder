#pragma once

#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "in_memory_types.h"

namespace rsrcloader {

typedef uint32_t ResType;
typedef uint16_t ResID;

class Resource {
 public:
  Resource(ResID id,
           ResType type,
           uint8_t attributes,
           std::string name,
           const uint8_t* const data_ptr,
           uint32_t size);

  ResID GetId() const { return id_; }
  ResType GetType() const { return type_; }
  const std::string& GetName() const { return name_; }
  uint8_t GetAttributes() const { return attributes_; }
  uint32_t GetSize() const { return size_; }
  const uint8_t* const GetData() const { return data_ptr_; }

  std::string GetTypeName() const;

 protected:
  // Disallow copy and assign:
  Resource(const Resource&) = delete;
  Resource& operator=(Resource&) = delete;

 private:
  friend std::ostream& operator<<(std::ostream&, const Resource&);

  const ResID id_;
  const ResType type_;
  const uint8_t attributes_;
  const std::string name_;
  const uint8_t* const data_ptr_;
  const uint32_t size_;
};

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

std::ostream& operator<<(std::ostream&, const Resource&);
std::ostream& operator<<(std::ostream&, const ResourceFile&);

}  // namespace rsrcloader