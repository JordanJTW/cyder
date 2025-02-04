// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <fstream>
#include <iostream>

#include "absl/strings/str_cat.h"
#include "core/endian_helpers.h"
#include "core/logging.h"
#include "core/memory_reader.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/rsrc/macbinary_helpers.h"
#include "emu/rsrc/resource_file.h"
#include "finder_flags.h"
#include "gen/typegen/typegen_prelude.h"

namespace {

using ::cyder::hfs::ParseFinderFlags;

absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
}

void DumpMemoryRegionTo(const core::MemoryRegion& region, std::string path) {
  std::ofstream output(path.c_str(), std::ios::out | std::ios::binary);
  output.write(reinterpret_cast<const char*>(region.raw_ptr()), region.size());
  LOG(INFO) << "Wrote " << region.size() << " bytes to \"" << path << "\"";
  output.close();
}

}  // namespace

absl::Status MaybeWriteNextRegion(core::MemoryReader& reader,
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
  auto header = TRY(ReadType<MacBinaryHeader>(memory));

  LOG(INFO) << "MacBinaryHeader: " << header;

  core::MemoryReader reader(memory, 128);
  RETURN_IF_ERROR(MaybeWriteNextRegion(reader, output_dir, header.filename,
                                       "data", header.data_length));
  RETURN_IF_ERROR(MaybeWriteNextRegion(reader, output_dir, header.filename,
                                       "rsrc", header.rsrc_length));

  for (auto flag : ParseFinderFlags(header.finder_flags)) {
    LOG(INFO) << "Finder Flag: " << flag;
  }

  uint16_t calculated_crc = TRY(MacBinaryChecksum(memory));
  LOG(INFO) << "Calculated CRC: " << calculated_crc;
  CHECK_EQ(calculated_crc, header.header_checksum);

  return absl::OkStatus();
}
