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
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/pict_v1.h"
#include "gen/typegen/typegen_prelude.h"

using ::cyder::FrameRectToBytes;
using ::cyder::PixelWidthToBytes;
using ::cyder::graphics::GetPICTFrame;
using ::cyder::graphics::ParsePICTv1;

namespace {
absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
}

void ParseIcon(const std::string& name,
               const uint8_t* const data,
               int height,
               int width) {
  std::ofstream icon;
  icon.open(absl::StrCat("/tmp/", name, ".ppm"), std::ios::out);
  auto write_byte = [&](uint8_t byte, int length) {
    length = 7 - length;
    for (int i = 7; i >= length; --i) {
      icon << ((byte & (1 << i)) ? "1 " : "0 ");
    }
  };

  icon << "P1 " << width << " " << height << "\n";

  int byte_width = PixelWidthToBytes(width);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < byte_width; ++x) {
      int bit_length = width - 8 * x;
      if (bit_length > 7) {
        bit_length = 7;
      } else {
        bit_length -= 1;
      }
      write_byte(data[x + y * byte_width], bit_length);
    }
    icon << "\n";
  }
}

}  // namespace

absl::Status Main(const core::Args& args) {
  auto path = TRY(args.GetArg(1, "FILENAME"));

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

  auto frame = TRY(GetPICTFrame(memory));

  size_t picture_size = FrameRectToBytes(frame);
  uint8_t picture[picture_size];
  std::memset(picture, 0, picture_size);

  RETURN_IF_ERROR(ParsePICTv1(memory, /*output=*/picture));

  ParseIcon("spam", picture, frame.bottom, frame.right);

  return absl::OkStatus();
}
