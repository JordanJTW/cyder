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

 private:
  Rect GetRectForGlyph(char glyph);

  core::MemoryReader reader_;
  FontResource header_;
  core::MemoryRegion image_table_;
  core::MemoryRegion location_table_;
};

ResFont::ResFont(const core::MemoryRegion& data)
    : reader_(data),
      header_(MUST(reader_.NextType<FontResource>())),
      image_table_(MUST(reader_.NextRegion(
          "image_table",
          header_.bit_image_row_width * header_.font_rect_height * 2))),
      location_table_(MUST(reader_.NextRegion(
          "location_table",
          (header_.last_char_code - header_.first_char_code) * 2))) {}

int ResFont::DrawString(graphics::BitmapImage& image,
                        absl::string_view string,
                        int x,
                        int y) {
  int x_offset = 0;
  for (int c : string) {
    // TODO: Figure out how to properly handle out of range characters...
    //       This probably has something to do with Apple having non-standard
    //       extended character sets and absl::string_view iterator returning
    //       signed chars (the positive half is the non-extended half).
    if (c < 0 || c > 255) {
      LOG(WARNING) << "Skipping out-of-range char: " << c;
      continue;
    }

    if (c == '\r') {
      x_offset = 0;
      y += header_.font_rect_height;
    }

    auto rect = GetRectForGlyph(c);

    image.CopyBits(image_table_.raw_ptr(),
                   NewRect(0, 0, header_.bit_image_row_width * 16,
                           header_.font_rect_height),
                   rect,
                   NewRect(x + x_offset, y, rect.right - rect.left,
                           header_.font_rect_height));

    x_offset += (rect.right - rect.left);
  }
  return x_offset;
}

int ResFont::DrawChar(graphics::BitmapImage& image, char ch, int x, int y) {
  auto rect = GetRectForGlyph(ch);

  image.CopyBits(
      image_table_.raw_ptr(),
      NewRect(0, 0, header_.bit_image_row_width * 16, header_.font_rect_height),
      rect, NewRect(x, y, rect.right - rect.left, header_.font_rect_height));

  return (rect.right - rect.left);
}

Rect ResFont::GetRectForGlyph(char glyph) {
  if (!(glyph >= header_.first_char_code && glyph <= header_.last_char_code))
    return NewRect(0, 0, 0, 0);

  auto select_char_index = glyph - header_.first_char_code;
  auto select_glyph_offset =
      MUST(location_table_.Read<int16_t>(select_char_index * 2));
  auto next_glyph_offset =
      MUST(location_table_.Read<int16_t>((select_char_index + 1) * 2));

  return Rect{.top = 0,
              .left = select_glyph_offset,
              .bottom = static_cast<int16_t>(header_.font_rect_height),
              .right = next_glyph_offset};
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
