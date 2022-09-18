#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <fstream>
#include <iostream>

#include "core/endian_helpers.h"
#include "core/logging.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "finder_flags.h"
#include "gen/typegen/typegen_prelude.h"
#include "resource.h"

namespace {

using cyder::hfs::ParseFinderFlags;
using rsrcloader::GetTypeName;

absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
}

class MemoryReader final {
 public:
  explicit MemoryReader(const core::MemoryRegion& region) : region_(region) {}

  template <typename IntegerType,
            typename std::enable_if<std::is_integral<IntegerType>::value,
                                    bool>::type = true>
  absl::StatusOr<IntegerType> Next() {
    IntegerType value = TRY(region_.Copy<IntegerType>(offset_));
    offset_ += sizeof(value);
    return betoh<IntegerType>(value);
  }

  absl::StatusOr<absl::string_view> NextString(
      absl::optional<size_t> fixed_size = absl::nullopt) {
    auto length = TRY(Next<uint8_t>());
    if (fixed_size.has_value() && fixed_size.value() < length) {
      return absl::FailedPreconditionError(absl::StrCat(
          "String has a length of ", length,
          " which is greater than its fixed size (", fixed_size.value(), ")"));
    }
    auto data = reinterpret_cast<const char*>(region_.raw_ptr()) + offset_;
    offset_ += fixed_size.value_or(length);
    return absl::string_view(data, length);
  }

  absl::StatusOr<core::MemoryRegion> NextRegion(std::string name,
                                                size_t length) {
    auto region = TRY(region_.Create(std::move(name), offset_, length));
    offset_ += length;
    return region;
  }

  void OffsetTo(size_t new_offset) { offset_ = new_offset; }

  void AlignTo(size_t block_size) {
    if (offset_ % block_size != 0) {
      OffsetTo(((offset_ / block_size) + 1 * block_size));
    }
  }

 private:
  const core::MemoryRegion region_;
  size_t offset_{0};
};

void DumpMemoryRegionTo(const core::MemoryRegion& region, std::string path) {
  std::ofstream output(path.c_str(), std::ios::out | std::ios::binary);
  output.write(reinterpret_cast<const char*>(region.raw_ptr()), region.size());
  LOG(INFO) << "Wrote " << region.size() << " bytes to \"" << path << "\"";
  output.close();
}

}  // namespace

absl::Status MaybeWriteNextRegion(MemoryReader& reader,
                                  absl::string_view output_dir,
                                  absl::string_view filename,
                                  std::string extension,
                                  size_t length) {
  if (length == 0) {
    return absl::OkStatus();
  }

  reader.AlignTo(128);
  auto region = TRY(reader.NextRegion(extension, length));
  DumpMemoryRegionTo(region,
                     absl::StrCat(output_dir, "/", filename, ".", extension));
  return absl::OkStatus();
}

// Implements CRC-16/XModem calculation
// Link: https://mdfs.net/Info/Comp/Comms/CRC16.htm, https://crccalc.com
uint16_t crc16(const core::MemoryRegion& region, uint16_t initial_crc = 0) {
  uint16_t crc = initial_crc;
  for (size_t i = 0; i < region.size(); ++i) {
    crc = crc ^ (region.raw_ptr()[i] << 8);
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

absl::Status Main(const core::Args& args) {
  auto path = TRY(args.GetArg(1, "INPUT"));
  auto output_dir = TRY(args.GetArg(2, "OUTPUT_DIR"));

  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return LoadFileError(path);
  }

  struct stat status;
  if (fstat(fd, &status) < 0) {
    return LoadFileError(path);
  }

  size_t size = status.st_size;
  void* mmap_ptr = mmap(0, size, PROT_READ, MAP_PRIVATE, fd, /*offset=*/0);
  if (mmap_ptr == MAP_FAILED) {
    return LoadFileError(path);
  }

  core::MemoryRegion memory(mmap_ptr, size);

  // MacBinary II Header
  // Link: https://files.stairways.com/other/macbinaryii-standard-info.txt
  // Link: https://github.com/mietek/theunarchiver/wiki/MacBinarySpecs
  MemoryReader reader(memory);
  CHECK_EQ(0u, TRY(reader.Next<uint8_t>()));
  auto filename = TRY(reader.NextString(/*fixed_length=*/63));
  auto file_type = TRY(reader.Next<uint32_t>());
  auto creator_type = TRY(reader.Next<uint32_t>());
  auto finder_flags_high = TRY(reader.Next<uint8_t>());
  CHECK_EQ(0u, TRY(reader.Next<uint8_t>()));
  auto finder_vert_pos = TRY(reader.Next<uint16_t>());
  auto finder_horr_pos = TRY(reader.Next<uint16_t>());
  auto finder_folder_id = TRY(reader.Next<uint16_t>());
  auto is_protected = TRY(reader.Next<uint8_t>());
  CHECK_EQ(0u, TRY(reader.Next<uint8_t>()));
  auto data_length = TRY(reader.Next<uint32_t>());
  auto rsrc_length = TRY(reader.Next<uint32_t>());
  // Dates are the number of seconds since Jan. 1, 1904:
  auto creation_date = TRY(reader.Next<uint32_t>());
  auto modified_date = TRY(reader.Next<uint32_t>());
  auto info_length = TRY(reader.Next<uint16_t>());
  auto finder_flags_low = TRY(reader.Next<uint8_t>());
  reader.OffsetTo(116);
  auto packed_files_count = TRY(reader.Next<uint32_t>());
  auto second_header_length = TRY(reader.Next<uint16_t>());
  auto macbinary_write_version = TRY(reader.Next<uint8_t>());
  auto macbinary_read_version = TRY(reader.Next<uint8_t>());
  auto header_crc = TRY(reader.Next<uint16_t>());

  RETURN_IF_ERROR(
      MaybeWriteNextRegion(reader, output_dir, filename, "data", data_length));
  RETURN_IF_ERROR(
      MaybeWriteNextRegion(reader, output_dir, filename, "rsrc", rsrc_length));

  LOG(INFO) << filename << " type: " << GetTypeName(file_type)
            << " creator: " << GetTypeName(creator_type)
            << " x: " << finder_horr_pos << ", y: " << finder_vert_pos
            << " folder id: " << finder_folder_id
            << " is_protected: " << (is_protected ? "true" : "false")
            << " data length: " << data_length
            << " rsrc length: " << rsrc_length << " created: " << creation_date
            << " modified: " << modified_date << " info length: " << info_length
            << " packed files count: " << packed_files_count
            << " second header length: " << second_header_length
            << " write version: " << int(macbinary_write_version)
            << " read version: " << int(macbinary_read_version)
            << " CRC: " << header_crc;

  uint16_t finder_flags = finder_flags_low;
  finder_flags = (finder_flags << 8) | finder_flags_high;

  for (auto flag : ParseFinderFlags(finder_flags)) {
    LOG(INFO) << "Finder Flag: " << flag;
  }

  uint16_t calculated_crc =
      crc16(TRY(memory.Create("crc", /*offset=*/0, /*size=*/124)));
  LOG(INFO) << "Calculated CRC: " << calculated_crc;
  CHECK_EQ(calculated_crc, header_crc);

  return absl::OkStatus();
}
