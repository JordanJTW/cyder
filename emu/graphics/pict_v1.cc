// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/graphics/pict_v1.h"

#include <cstddef>
#include <fstream>

#include "core/logging.h"
#include "core/status_helpers.h"
#include "emu/graphics/copybits.h"
#include "emu/graphics/graphics_helpers.h"

namespace cyder {
namespace graphics {
namespace {

Rect RelativeTo(Rect container, Rect target) {
  uint16_t offset_x = container.left;
  uint16_t offset_y = container.top;
  target.left -= offset_x;
  target.right -= offset_x;
  target.top -= offset_y;
  target.bottom -= offset_y;
  return target;
}

int PixelWidthToBytes(int width_px) {
  int width_bytes = width_px / CHAR_BIT;
  // Add one byte to capture the remaining pixels if needed
  return width_px % CHAR_BIT ? width_bytes + 1 : width_bytes;
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
    if (static_cast<uint8_t>(flag) == 0x80) {
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

}  // namespace

absl::StatusOr<Rect> GetPICTFrame(const core::MemoryRegion& region) {
  core::MemoryReader reader(region);

  /*pict_size=*/TRY(reader.Next<uint16_t>());
  auto frame = TRY(reader.NextType<Rect>());
  LOG(INFO) << "PICT Frame: { " << frame << " }";

  return NormalizeRect(frame);
}

absl::Status ParsePICTv1(const core::MemoryRegion& region, uint8_t* output) {
  core::MemoryReader reader(region);
  /*pict_size=*/TRY(reader.Next<uint16_t>());
  auto pict_rect = TRY(reader.NextType<Rect>());

  auto normalized = NormalizeRect(pict_rect);

  size_t row_size = PixelWidthToBytes(normalized.right);

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
                        output + row_size * (dstRect.top + row), dstRect.left);
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
                        output + row_size * (dstRect.top + row), 0);
        }
        break;
      }

      case 0xFF: {
        LOG(INFO) << "EndOfPicture";
        return absl::OkStatus();
      }

      default:
        return absl::UnimplementedError(
            absl::StrCat("Unknown op-code: 0x", absl::Hex(opcode)));
    }
  }
  return absl::FailedPreconditionError("Failed to find EndOfPicture");
}

}  // namespace graphics
}  // namespace cyder