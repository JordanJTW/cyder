#include "memory_region.h"

#include <algorithm>
#include <cctype>
#include <iomanip>

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

std::ostream& operator<<(std::ostream& os, const MemoryRegion& region) {
  constexpr size_t bytes_per_line = 16;

  size_t line_count = region.size() / bytes_per_line + 1;
  for (int line = 0; line < line_count; ++line) {
    size_t start_index = line * bytes_per_line;
    size_t full_line = start_index + bytes_per_line;
    size_t end_index = std::min(full_line, region.size());

    os << std::setfill('0') << std::setw(6) << std::hex
       << region.base_offset() + start_index << "\t";
    for (int index = start_index; index < end_index; ++index) {
      os << " " << std::setfill('0') << std::setw(2) << std::hex
         << (int)region.raw_ptr()[index];
    }

    // Ensure that lines are always printed to the full width so
    // that the ASCII column (below) is aligned when displayed
    for (int index = end_index; index < full_line; ++index) {
      os << "   ";
    }

    os << "\t|";
    for (int index = start_index; index < end_index; ++index) {
      uint8_t character = region.raw_ptr()[index];
      if (std::isprint(character)) {
        os << character;
      } else {
        os << '.';
      }
    }
    os << "|\n";
  }
  // Ensure std::hex does not "infect" other streams
  return os << std::dec;
}

}  // namespace rsrcloader