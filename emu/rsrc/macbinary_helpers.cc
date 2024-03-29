#include "emu/rsrc/macbinary_helpers.h"

#include "core/logging.h"
#include "gen/typegen/typegen_prelude.h"

template <>
absl::StatusOr<MacBinaryHeader> ReadType(const core::MemoryRegion& region,
                                         size_t offset) {
  core::MemoryReader reader(region, offset);

  struct MacBinaryHeader header;
  header.is_valid = TRY(reader.Next<uint8_t>()) == 0u;  // Byte 00
  header.filename = std::string(TRY(reader.NextString(/*fixed_length=*/63)));
  header.file_type = TRY(reader.Next<uint32_t>());
  header.creator_type = TRY(reader.Next<uint32_t>());
  // Finder flags (8-15) to be combined with (0-7) below
  header.finder_flags = TRY(reader.Next<uint8_t>());
  header.is_valid &= TRY(reader.Next<uint8_t>()) == 0u;  // Byte 74
  header.finder_position = TRY(reader.NextType<Point>());
  header.folder_id = TRY(reader.Next<uint16_t>());
  header.is_protected = TRY(reader.Next<uint8_t>()) != 0;
  header.is_valid &= TRY(reader.Next<uint8_t>()) == 0u;  // Byte 82
  header.data_length = TRY(reader.Next<uint32_t>());
  header.rsrc_length = TRY(reader.Next<uint32_t>());
  // Dates are the number of seconds since Jan. 1, 1904 (HFS Epoch):
  header.created_timestamp =
      absl::FromUnixSeconds(TRY(reader.Next<uint32_t>()) - 2082844800);
  header.modified_timestamp =
      absl::FromUnixSeconds(TRY(reader.Next<uint32_t>()) - 2082844800);
  header.info_length = TRY(reader.Next<uint16_t>());

  header.finder_flags <<= 8;
  header.finder_flags |= TRY(reader.Next<uint8_t>());

  reader.OffsetTo(116);
  header.packed_files_count = TRY(reader.Next<uint32_t>());
  header.secondary_header_length = TRY(reader.Next<uint16_t>());
  header.macbinary_write_version = TRY(reader.Next<uint8_t>());
  header.macbinary_read_version = TRY(reader.Next<uint8_t>());
  header.header_checksum = TRY(reader.Next<uint16_t>());

  header.is_valid &= (header.header_checksum ==
                      TRY(MacBinaryChecksum(TRY(region.Create(offset)))));
  return header;
}

// Implements CRC-16/XModem calculation
// Link: https://mdfs.net/Info/Comp/Comms/CRC16.htm, https://crccalc.com
absl::StatusOr<uint16_t> MacBinaryChecksum(const core::MemoryRegion& region) {
  // The header is 128 bytes minus 4 bytes to store the checksum
  static size_t kHeaderSize = 124;
  auto header_region = TRY(region.Create("crc", /*offset=*/0, kHeaderSize));

  uint16_t crc = 0;
  for (size_t i = 0; i < kHeaderSize; ++i) {
    crc = crc ^ (header_region.raw_ptr()[i] << 8);
    for (int byte = 0; byte < 8; ++byte) {
      if (crc & 0x8000) {
        crc = (crc << 1) ^ 0x1021;
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

std::ostream& operator<<(std::ostream& os, const MacBinaryHeader& header) {
  return os << "{ filename: '" << header.filename
            << "', type: " << OSTypeName(header.file_type)
            << ", creator: " << OSTypeName(header.creator_type)
            << ", finder_flags: " << header.finder_flags
            << ", finder_position: " << header.finder_position
            << ", folder_id: " << header.folder_id
            << ", is_protected: " << (header.is_protected ? "True" : "False")
            << ", data_length: " << header.data_length
            << ", rsrc_length: " << header.rsrc_length
            << ", created_timestamp: " << header.created_timestamp
            << ", modified_timestamp: " << header.modified_timestamp
            << ", info_length: " << header.info_length
            << ", packed_files_count: " << header.packed_files_count
            << ", secondary_header_length: " << header.secondary_header_length
            << ", macbinary_write_version: "
            << int(header.macbinary_write_version)
            << ", macbinary_read_version: "
            << int(header.macbinary_read_version)
            << ", header_checksum: " << header.header_checksum
            << ", is_valid: " << (header.is_valid ? "True" : "False") << " }";
}