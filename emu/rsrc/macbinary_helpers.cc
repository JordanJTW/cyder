#include "emu/rsrc/macbinary_helpers.h"

#include "core/logging.h"
#include "gen/typegen/typegen_prelude.h"

template <>
absl::StatusOr<MacBinaryHeader> ReadType(const core::MemoryRegion& region,
                                         size_t offset) {
  core::MemoryReader reader(region, offset);

  struct MacBinaryHeader header;
  CHECK_EQ(0u, TRY(reader.Next<uint8_t>()));
  header.filename = std::string(TRY(reader.NextString(/*fixed_length=*/63)));
  header.file_type = TRY(reader.Next<uint32_t>());
  header.creator_type = TRY(reader.Next<uint32_t>());
  // Finder flags (8-15) to be combined with (0-7) below
  header.finder_flags = TRY(reader.Next<uint8_t>());
  CHECK_EQ(0u, TRY(reader.Next<uint8_t>()));
  header.finder_position = TRY(reader.NextType<Point>());
  header.folder_id = TRY(reader.Next<uint16_t>());
  header.is_protected = TRY(reader.Next<uint8_t>()) != 0;
  CHECK_EQ(0u, TRY(reader.Next<uint8_t>()));
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
  return header;
}

std::ostream& operator<<(std::ostream&os , const MacBinaryHeader& header) {
  return os << "{ filename: '" << header.filename
            << "', type: " << header.file_type
            << ", creator: " << header.creator_type
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
            << ", header_checksum: " << header.header_checksum << " }";
}