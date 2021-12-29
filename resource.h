#include <cstdint>
#include <ostream>
#include <string>

#include "absl/status/statusor.h"
#include "in_memory_types.h"
#include "memory_region.h"

namespace rsrcloader {

typedef uint32_t ResType;
typedef uint16_t ResID;

class Resource {
 public:
  Resource(ResID id,
           ResType type,
           uint8_t attributes,
           std::string name,
           const MemoryRegion& data,
           uint32_t size);

  static absl::StatusOr<std::unique_ptr<Resource>> Load(
      const InMemoryTypeItem& type_item,
      const MemoryRegion& type_list_region,
      const MemoryRegion& name_list_region,
      const MemoryRegion& data_region,
      size_t index);

  ResID GetId() const { return id_; }
  ResType GetType() const { return type_; }
  const std::string& GetName() const { return name_; }
  uint8_t GetAttributes() const { return attributes_; }
  uint32_t GetSize() const { return size_; }
  const MemoryRegion& GetData() const { return data_; }

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
  const MemoryRegion data_;
  const uint32_t size_;
};

std::ostream& operator<<(std::ostream&, const Resource&);

}  // namespace rsrcloader