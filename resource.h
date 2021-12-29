#include <cstdint>
#include <ostream>
#include <string>

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

  static absl::StatusOr<std::unique_ptr<Resource>> Load(
      const uint8_t* const base_ptr,
      size_t size,
      const InMemoryMapHeader& header,
      const InMemoryTypeItem& type_item,
      size_t index);

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

std::ostream& operator<<(std::ostream&, const Resource&);

}  // namespace rsrcloader