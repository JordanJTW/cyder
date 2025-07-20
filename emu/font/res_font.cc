#include <utility>

#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "core/memory_region.h"
#include "emu/font/font.h"
#include "emu/font/font_types.tdef.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/rsrc/resource_manager.h"

namespace cyder {
namespace {

class ResFont : public Font {
 public:
  ResFont(const core::MemoryRegion& data);

  // Font implementation:
  int DrawString(graphics::BitmapImage& image,
                 absl::string_view string,
                 int x,
                 int y) override;
  int DrawChar(graphics::BitmapImage& image, char ch, int x, int y) override;
  int GetCharWidth(char ch) override;
  int GetStringWidth(absl::string_view string) override;
  FontInfo GetFontInfo() override;

 private:
  Rect GetRectForGlyph(char glyph);
  std::pair<int8_t, int8_t> GetOffsetAndWidthForGlyph(char ch);

  core::MemoryReader reader_;
  FontResource header_;
  core::MemoryRegion image_table_;
  core::MemoryRegion location_table_;
  core::MemoryRegion width_offset_table_;
};

ResFont::ResFont(const core::MemoryRegion& data)
    : reader_(data),
      header_(MUST(reader_.NextType<FontResource>())),
      image_table_(MUST(reader_.NextRegion(
          "image_table",
          header_.bit_image_row_width * header_.font_rect_height * 2))),
      location_table_(MUST(reader_.NextRegion(
          "location_table",
          (header_.last_char_code - header_.first_char_code) * 2))),
      // An integer value that specifies the offset to the offset/width table
      // from this point in the font record, in words.
      // https://developer.apple.com/library/archive/documentation/mac/Text/Text-250.html
      width_offset_table_(MUST(data.Create(
          "width_offset_table",
          /*offset=*/FontResourceFields::offset_width_table.offset +
              (header_.offset_width_table * sizeof(uint16_t)),
          /*size=*/(header_.last_char_code - header_.first_char_code) * 2))) {}

int ResFont::DrawString(graphics::BitmapImage& image,
                        absl::string_view string,
                        int x,
                        int y) {
  int x_offset = 0;
  for (int ch : string) {
    // TODO: Figure out how to properly handle out of range characters...
    //       This probably has something to do with Apple having non-standard
    //       extended character sets and absl::string_view iterator returning
    //       signed chars (the positive half is the non-extended half).
    if (ch < 0 || ch > 255) {
      LOG(WARNING) << "Skipping out-of-range char: " << ch;
      continue;
    }

    if (ch == '\r') {
      x_offset = 0;
      y += header_.font_rect_height;
    }

    x_offset += DrawChar(image, ch, x + x_offset, y);
  }
  return x_offset;
}

int ResFont::DrawChar(graphics::BitmapImage& image, char ch, int x, int y) {
  if (!(ch >= header_.first_char_code && ch <= header_.last_char_code)) {
    LOG(WARNING) << "Skipping missing '" << ch << "' in font";
    return header_.font_rect_width;
  }

  auto rect = GetRectForGlyph(ch);
  auto offset_and_width = GetOffsetAndWidthForGlyph(ch);

  image.CopyBits(
      image_table_.raw_ptr(),
      NewRect(0, 0, header_.bit_image_row_width * 16, header_.font_rect_height),
      rect,
      // |x| represents the glyph-origin. The value of the offset, when added to
      // the maximum kerning value for the font, determines the horizontal
      // distance from the glyph origin to the left edge of the bit image of the
      // glyph, in pixels. If this sum is negative, the glyph origin is to the
      // right of the glyph image's left edge, meaning the glyph kerns to the
      // left. If the sum is positive, the origin is to the left of the image's
      // left edge. If the sum equals zero, the glyph origin corresponds with
      // the left edge of the bit image.
      NewRect(x + offset_and_width.first + header_.max_kerning, y,
              rect.right - rect.left, header_.font_rect_height));

  // The width represents the glyph-origin to next glyph-origin delta.
  return offset_and_width.second;
}

int ResFont::GetCharWidth(char ch) {
  return GetOffsetAndWidthForGlyph(ch).second;
}

int ResFont::GetStringWidth(absl::string_view string) {
  int total_width = 0;
  for (char ch : string)
    total_width += GetOffsetAndWidthForGlyph(ch).second;
  return total_width;
}

FontInfo ResFont::GetFontInfo() {
  return {.ascent = header_.max_ascent,
          .descent = header_.max_descent,
          .width_max = header_.font_rect_width,
          .leading = header_.leading};
}

Rect ResFont::GetRectForGlyph(char ch) {
  auto select_char_index = ch - header_.first_char_code;
  auto select_glyph_offset =
      MUST(location_table_.Read<int16_t>(select_char_index * 2));
  auto next_glyph_offset =
      MUST(location_table_.Read<int16_t>((select_char_index + 1) * 2));

  return Rect{.top = 0,
              .left = select_glyph_offset,
              .bottom = static_cast<int16_t>(header_.font_rect_height),
              .right = next_glyph_offset};
}

std::pair<int8_t, int8_t> ResFont::GetOffsetAndWidthForGlyph(char ch) {
  auto select_char_index = ch - header_.first_char_code;
  // Width/offset table. For every glyph in the font, this table contains a word
  // with the glyph offset in the high-order byte and the glyph's width, in
  // integer form, in the low-order byte.
  return {MUST(width_offset_table_.Read<int8_t>(select_char_index * 2)),
          MUST(width_offset_table_.Read<int8_t>(select_char_index * 2 + 1))};
}

class FontManager {
 public:
  Font& GetFont(int16_t font_type) {
    if (loaded_fonts_.contains(font_type)) {
      return *loaded_fonts_[font_type];
    }

    auto ids_and_names = ResourceManager::the().GetIdsForType('FONT');
    for (auto& [id, name] : ids_and_names) {
      if (!name.empty())
        continue;

      // Font families were created by storing a unique family ID in bits 7-14
      // of the resource ID of each font in the family. Link:
      // https://dev.os9.ca/techpubs/mac/Text/Text-189.html
      if (((id >> 7) & 0xFF) == font_type) {
        Handle handle = ResourceManager::the().GetResource('FONT', id);

        auto font = std::make_unique<ResFont>(
            memory::MemoryManager::the().GetRegionForHandle(handle));
        loaded_fonts_[font_type] = std::move(font);
        return *loaded_fonts_[font_type];
      }
    }
    return BuiltInFont();
  }

 private:
  absl::flat_hash_map<uint16_t, std::unique_ptr<Font>> loaded_fonts_;
};

}  // namespace

Font& GetFont(int16_t font_type) {
  static absl::NoDestructor<FontManager> sInstance;
  return sInstance->GetFont(font_type);
}

}  // namespace cyder
