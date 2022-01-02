#include "memory_region.h"

namespace rsrcloader {

MemoryRegion::MemoryRegion(void* data, size_t size)
    : MemoryRegion("Base",
                   reinterpret_cast<const uint8_t* const>(data),
                   size,
                   /*maximum_size=*/size,
                   /*base_offset=*/0) {}

absl::StatusOr<MemoryRegion> MemoryRegion::Create(std::string name,
                                                  size_t offset) const {
  return Create(std::move(name), offset, 0);
}

absl::StatusOr<MemoryRegion> MemoryRegion::Create(std::string name,
                                                  size_t offset,
                                                  size_t size) const {
  // Prevent creating a region which overflows the base data
  if (maximum_size_ < offset + size) {
    return absl::OutOfRangeError(absl::StrCat("Offset [", offset, ":+", size,
                                              "] overflows ", maximum_size_));
  }
  // Being outside of the parent is undesirable but not a hard fault
  if (size_ && size_ < offset + size) {
    LOG(WARNING) << "Offset [" << offset << ":+" << size << "] is outside "
                 << size_;
  }
  return MemoryRegion(std::move(name), data_ + offset, size,
                      maximum_size_ - offset, base_offset_ + offset);
}

absl::Status MemoryRegion::Copy(void* dest,
                                size_t offset,
                                size_t length) const {
  if (maximum_size_ < offset + length) {
    return absl::OutOfRangeError(absl::StrCat("Overflow"));
  }
  if (size_ && size_ < offset + length) {
    LOG(WARNING) << "Reading " << (offset + length - size_)
                 << " bytes outside of '" << name_ << "' region";
  }
  memcpy(dest, data_ + offset, length);
  return absl::OkStatus();
}

MemoryRegion::MemoryRegion(std::string name,
                           const uint8_t* const data,
                           size_t size,
                           size_t maximum_size,
                           size_t base_offset)
    : name_(std::move(name)),
      data_(data),
      size_(size),
      maximum_size_(maximum_size),
      base_offset_(base_offset) {}

}  // namespace rsrcloader