#include "memory_region.h"

#include <algorithm>
#include <cctype>
#include <iomanip>

#define CHECK_SAFE_ACCESS(offset, size) \
  RETURN_IF_ERROR(CheckSafeAccess(__func__, offset, size))

namespace core {

MemoryRegion::MemoryRegion(void* const data, size_t size)
    : MemoryRegion("Base",
                   reinterpret_cast<uint8_t* const>(data),
                   size,
                   /*maximum_size=*/size,
                   /*base_offset=*/0) {}

absl::StatusOr<MemoryRegion> MemoryRegion::Create(size_t offset) const {
  return Create(/*name=*/"", offset);
}

absl::StatusOr<MemoryRegion> MemoryRegion::Create(std::string name,
                                                  size_t offset) const {
  size_t new_size = size_ > offset ? size_ - offset : 0;
  return Create(std::move(name), offset, new_size);
}

absl::StatusOr<MemoryRegion> MemoryRegion::Create(std::string name,
                                                  size_t offset,
                                                  size_t size) const {
  CHECK_SAFE_ACCESS(offset, size);

  return MemoryRegion(std::move(name), data_ + offset, size,
                      maximum_size_ - offset, base_offset_ + offset);
}

absl::Status MemoryRegion::Copy(void* dest,
                                size_t offset,
                                size_t length) const {
  CHECK_SAFE_ACCESS(offset, length);

  memcpy(dest, data_ + offset, length);
  return absl::OkStatus();
}

absl::Status MemoryRegion::Write(const void* src,
                                 size_t offset,
                                 size_t length) {
  CHECK_SAFE_ACCESS(offset, length);

  memcpy(data_ + offset, src, length);
  return absl::OkStatus();
}

template <>
absl::Status MemoryRegion::Write(size_t offset, const MemoryRegion& data) {
  return Write(data.data_, offset, data.size());
}

absl::Status MemoryRegion::CheckSafeAccess(const std::string& access_type,
                                           size_t offset,
                                           size_t size) const {
  // Prevent access which would overflow the base data (segfault)
  if (maximum_size_ < offset + size) {
    return absl::OutOfRangeError(
        absl::StrCat("Overflow reading from \"", name_, "\" offset: ", offset,
                     " + size: ", size, " > maximum size: ", maximum_size_));
  }
  // Warn but do not _prevent_ accesses outside preferred size
  if (size_ && size_ < offset + size) {
    LOG(WARNING) << access_type << " " << (offset + size - size_)
                 << " bytes outside of '" << name_ << "' region";
  }
  return absl::OkStatus();
}

MemoryRegion::MemoryRegion(std::string name,
                           uint8_t* const data,
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

  size_t initial_index =
      (region.base_offset() / bytes_per_line) * bytes_per_line;
  size_t total_from_start =
      region.base_offset() - initial_index + region.size();

  size_t line_count = (total_from_start / bytes_per_line);
  if (total_from_start % bytes_per_line) {
    line_count += 1;
  }
  for (size_t line = 0; line < line_count; ++line) {
    size_t start_index = initial_index + line * bytes_per_line;
    size_t end_index = start_index + bytes_per_line;

    os << std::setfill('0') << std::setw(6) << std::hex << start_index << "\t";

    for (size_t index = start_index; index < end_index; ++index) {
      if (index != start_index && index % 8 == 0) {
        os << "   ";
      }

      // Ensure that lines are always printed to the full width so
      // that the ASCII column (below) is aligned when displayed
      if (index < region.base_offset() ||
          index > initial_index + total_from_start) {
        os << "   ";
        continue;
      }

      os << " " << std::setfill('0') << std::setw(2) << std::hex
         << (int)region.raw_ptr()[index - region.base_offset()];
    }

    os << "\t|";
    for (size_t index = start_index; index < end_index; ++index) {
      if (index < region.base_offset() ||
          index > initial_index + total_from_start) {
        os << " ";
        continue;
      }

      uint8_t character = region.raw_ptr()[index - region.base_offset()];
      if (std::isprint(character)) {
        os << character;
      } else {
        os << '.';
      }
    }
    os << "|";
    if (line + 1 != line_count) {
      os << "\n";
    }
  }
  // Ensure std::hex does not "infect" other streams
  return os << std::dec;
}

}  // namespace core