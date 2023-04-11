#pragma once

#include <cstdint>
#include <string>

#include "absl/time/time.h"
#include "core/memory_reader.h"
// TODO: Move Point to a central location
#include "emu/graphics/grafport_types.tdef.h"

// MacBinary II Header
// Link: https://files.stairways.com/other/macbinaryii-standard-info.txt
// Link: https://github.com/mietek/theunarchiver/wiki/MacBinarySpecs
struct MacBinaryHeader {
  std::string filename;
  uint32_t file_type;
  uint32_t creator_type;
  // Combines Finder flags (high/low)
  uint16_t finder_flags;
  Point finder_position;
  uint16_t folder_id;
  bool is_protected;
  uint32_t data_length;
  uint32_t rsrc_length;
  absl::Time created_timestamp;
  absl::Time modified_timestamp;
  uint16_t info_length;
  uint32_t packed_files_count;
  uint16_t secondary_header_length;
  uint8_t macbinary_write_version;
  uint8_t macbinary_read_version;
  uint16_t header_checksum;
  bool is_valid = false;

  const static size_t fixed_size = 128;
};

template <typename MacBinaryHeader>
absl::StatusOr<MacBinaryHeader> ReadType(const core::MemoryRegion& region,
                                         size_t offset);

// Calculates the MacBinary header checksum from the first 124 bytes of region
absl::StatusOr<uint16_t> MacBinaryChecksum(const core::MemoryRegion&);

inline size_t MacBinaryRsrcOffset(const MacBinaryHeader& header) {
  static size_t kBlockSize = 128u;
  if (header.data_length % kBlockSize) {
    return MacBinaryHeader::fixed_size +
           ((header.data_length / kBlockSize) + 1) * kBlockSize;
  }
  return MacBinaryHeader::fixed_size + header.data_length;
}

std::ostream& operator<<(std::ostream&, const MacBinaryHeader&);