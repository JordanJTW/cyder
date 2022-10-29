#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>
#include <fstream>
#include <iostream>

#include "absl/strings/str_cat.h"
#include "bin/copybits.h"
#include "core/endian_helpers.h"
#include "core/logging.h"
#include "core/memory_reader.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/graphics/grafport_types.h"
#include "gen/typegen/typegen_prelude.h"

namespace {
absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
}

int PixelWidthToBytes(int width_px) {
  int width_bytes = width_px / CHAR_BIT;
  // Add one byte to capture the remaining pixels if needed
  return width_px % CHAR_BIT ? width_bytes + 1 : width_bytes;
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

absl::Status UnpackBits(core::MemoryReader& src,
                        uint8_t* dest,
                        size_t dst_size) {
  size_t unpacked_index = 0;
  uint8_t packed_index = 0;
  auto length = TRY(src.Next<uint8_t>());
  while (unpacked_index < dst_size) {
    auto flag = TRY(src.Next<int8_t>());
    packed_index++;
    if (flag == 0x80) {
      dest[unpacked_index++] = flag;
    } else if (flag < 0) {
      auto repeat = TRY(src.Next<uint8_t>());
      packed_index++;
      for (int i = -flag; i >= 0; --i) {
        dest[unpacked_index++] = repeat;
      }
    } else {
      for (int i = flag; i >= 0; --i) {
        dest[unpacked_index++] = TRY(src.Next<uint8_t>());
        packed_index++;
      }
    }
  }
  CHECK_EQ(packed_index, length);
  return absl::OkStatus();
}

Rect NormalizeRect(Rect rect) {
  uint16_t offset_x = rect.left;
  uint16_t offset_y = rect.top;
  rect.left -= offset_x;
  rect.right -= offset_x;
  rect.top -= offset_y;
  rect.bottom -= offset_y;
  return rect;
}

Rect RelativeTo(Rect container, Rect target) {
  uint16_t offset_x = container.left;
  uint16_t offset_y = container.top;
  target.left -= offset_x;
  target.right -= offset_x;
  target.top -= offset_y;
  target.bottom -= offset_y;
  return target;
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
  core::MemoryReader reader(memory);

  auto pict_size = TRY(reader.Next<uint16_t>());

  LOG(INFO) << "Memory size: " << memory.size() << " PICT size: " << pict_size;

  auto pict_rect = TRY(reader.NextType<Rect>());

  LOG(INFO) << "Rect: { " << pict_rect << " }";

  auto normalized = NormalizeRect(pict_rect);
  LOG(INFO) << "Normalized Rect: { " << normalized << " }";

  size_t row_size = PixelWidthToBytes(normalized.right);
  size_t picture_size = row_size * normalized.bottom;
  uint8_t picture[picture_size];
  std::memset(picture, 0, picture_size);

  while (reader.HasNext()) {
    auto opcode = TRY(reader.Next<uint8_t>());

    switch (opcode) {
      // clipRgn
      case 0x01: {
        auto region = TRY(reader.NextType<Region>());
        LOG(INFO) << "ClipRegion(region: { " << region << "})";
        break;
      }

      // picVersion
      case 0x11: {
        auto version = TRY(reader.Next<uint8_t>());
        LOG(INFO) << "PICT version: " << (int)version;
        break;
      }

      // shortComment
      case 0xa0: {
        auto kind = TRY(reader.Next<uint16_t>());
        LOG(INFO) << "shortComment kind: " << kind;
        break;
      }

      case 0x90: {
        auto row_bytes = TRY(reader.Next<uint16_t>());
        auto bounds = TRY(reader.NextType<Rect>());
        auto srcRect = TRY(reader.NextType<Rect>());
        auto dstRect = TRY(reader.NextType<Rect>());
        auto mode = TRY(reader.Next<uint16_t>());

        srcRect = RelativeTo(bounds, srcRect);
        dstRect = RelativeTo(pict_rect, dstRect);
        bounds = NormalizeRect(bounds);

        LOG(INFO) << "BitsRect(rowBytes: " << row_bytes << ", bounds: { "
                  << bounds << "}, srcRect: {" << srcRect << "}, dstRect: { "
                  << dstRect << " }, mode: " << mode << ")";

        size_t height = bounds.bottom - bounds.top;

        for (size_t row = 0; row < height; ++row) {
          auto row_region = TRY(reader.NextRegion("row", row_bytes));
          bitarray_copy(row_region.raw_ptr(), srcRect.left,
                        srcRect.right - srcRect.left,
                        picture + row_size * (dstRect.top + row), dstRect.left);
        }

        break;
      }

      case 0x98: {
        auto row_bytes = TRY(reader.Next<uint16_t>());
        auto bounds = TRY(reader.NextType<Rect>());
        auto srcRect = TRY(reader.NextType<Rect>());
        auto dstRect = TRY(reader.NextType<Rect>());
        auto mode = TRY(reader.Next<uint16_t>());

        srcRect = RelativeTo(bounds, srcRect);
        dstRect = RelativeTo(pict_rect, dstRect);
        bounds = NormalizeRect(bounds);

        LOG(INFO) << "PackedBitsRect(rowBytes: " << row_bytes << ", bounds: { "
                  << bounds << "}, srcRect: {" << srcRect << "}, dstRect: { "
                  << dstRect << " }, mode: " << mode << ")";

        uint8_t unpacked_bytes[row_bytes];
        size_t height = bounds.bottom - bounds.top;

        for (size_t row = 0; row < height; ++row) {
          RETURN_IF_ERROR(UnpackBits(reader, unpacked_bytes, row_bytes));
          bitarray_copy(unpacked_bytes, srcRect.left,
                        srcRect.right - srcRect.left,
                        picture + row_size * (dstRect.top + row), 0);
        }
        break;
      }

      case 0xFF: {
        LOG(INFO) << "EndOfPicture";
        ParseIcon("pict", picture, normalized.bottom, normalized.right);
        break;
      }

      default:
        LOG(FATAL) << "Unknown op-code: 0x" << std::hex << (int)opcode;
    }
  }

  return absl::OkStatus();
}
