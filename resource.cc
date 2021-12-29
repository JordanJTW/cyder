#include "resource.h"

#include <cstring>
#include <iomanip>

#include "core/endian.h"

namespace rsrcloader {

std::string Resource::GetTypeName() const {
  char type_name[4];
  // The type value is actually a 4 byte string so we must reverse it
  // back to big endian for the text to appear correctly
  uint32_t reversed_type = htobe32(type_);
  memcpy(type_name, &reversed_type, sizeof(uint32_t));
  return type_name;
}

Resource::Resource(uint16_t id,
                   uint32_t type,
                   uint8_t attributes,
                   std::string name,
                   const uint8_t* const data_ptr,
                   uint32_t size)
    : id_(id),
      type_(type),
      attributes_(attributes),
      name_(std::move(name)),
      data_ptr_(data_ptr),
      size_(size) {}

std::ostream& operator<<(std::ostream& out, const Resource& value) {
  return out << "Resource '" << value.GetTypeName() << "' (" << std::setw(5)
             << value.id_ << ") is " << value.size_ << " bytes";
}

}  // namespace rsrcloader
