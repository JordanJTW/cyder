#include "memory_region.h"

#include "core/logging.h"

namespace rsrcloader {

MemoryRegion::MemoryRegion(void* data, size_t size)
    : MemoryRegion("Base", reinterpret_cast<const uint8_t* const>(data), size) {
}

absl::StatusOr<MemoryRegion> MemoryRegion::Create(std::string name,
                                                  size_t offset) const {
  if (size_ < offset) {
    return absl::OutOfRangeError(
        absl::StrCat("Offset ", offset, " is outside ", size_));
  }
  return MemoryRegion(std::move(name), data_ + offset, size_ - offset);
}

absl::Status MemoryRegion::Copy(void* dest,
                                size_t offset,
                                size_t length) const {
  if (size_ < offset + length) {
    return absl::OutOfRangeError(absl::StrCat("Overflow"));
  }
  memcpy(dest, data_ + offset, length);
  return absl::OkStatus();
}

MemoryRegion::MemoryRegion(std::string name,
                           const uint8_t* const data,
                           size_t size)
    : name_(std::move(name)), data_(data), size_(size) {
}

}  // namespace rsrcloader