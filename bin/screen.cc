#include <SDL.h>

#include <bitset>

#include "core/logging.h"
#include "core/memory_reader.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/graphics/graphics_helpers.h"

namespace {

constexpr SDL_Color kOnColor = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr SDL_Color kOffColor = {0x00, 0x00, 0x00, 0xFF};

constexpr uint8_t kBlack[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kGrey[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
constexpr uint8_t kWhite[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr size_t kScreenWidth = 512;
constexpr size_t kScreenHeight = 384;

constexpr size_t kScaleFactor = 1;

class BitmapScreen {
  static const bool kVerbose = false;

 public:
  BitmapScreen(size_t width, size_t height)
      : width_(width),
        height_(height),
        bitmap_size_(PixelWidthToBytes(width) * height),
        bitmap_(new uint8_t[bitmap_size_]) {
    std::memset(bitmap_, 0, bitmap_size_);
  }

  ~BitmapScreen() { delete[] bitmap_; }

  void FillRow(size_t row, int16_t start, int16_t end, uint8_t pattern) {
    static const unsigned char kMask[] = {0b11111111, 0b01111111, 0b00111111,
                                          0b00011111, 0b00001111, 0b00000111,
                                          0b00000011, 0b00000001, 0b00000000};

    size_t start_byte = row * PixelWidthToBytes(width_) + (start / CHAR_BIT);

    LOG_IF(INFO, kVerbose) << "Start byte: " << start_byte;

    int remaining_pixels = (end - start);

    // Handles a |start| offset which is not byte aligned. Once this is handled
    // the next pixel is gauranteed to appear at the start of the next byte.
    uint8_t start_offset = start % CHAR_BIT;
    if (start_offset) {
      // Handles a corner case where the |start| and |end| both occur in the
      // middle of the start byte.
      int byte_aligned_size = start_offset + (end - start);
      if (byte_aligned_size <= CHAR_BIT) {
        // Calculate a mask so that only the bits in the middle are on
        uint8_t byte_mask = kMask[start_offset] & ~kMask[byte_aligned_size];
        // Clear the pixels to be written to 0
        bitmap_[start_byte] &= ~byte_mask;
        // Draw the masked pattern
        bitmap_[start_byte] |= (byte_mask & pattern);
        return;
      }

      LOG_IF(INFO, kVerbose)
          << "Start inset: " << (int)start_offset
          << " Mask: " << std::bitset<8>(kMask[start_offset]);

      // Clear the pixels to be written to 0
      bitmap_[start_byte] &= ~kMask[start_offset];
      // Draw the masked pattern
      bitmap_[start_byte] |= (kMask[start_offset] & pattern);
      remaining_pixels -= (CHAR_BIT - start_offset);
      start_byte += 1;
    }

    // Now we are byte aligned so try to write out as many full bytes as we can
    int full_bytes = remaining_pixels / CHAR_BIT;
    if (full_bytes) {
      LOG_IF(INFO, kVerbose) << "Full bytes: " << full_bytes;
      std::memset(bitmap_ + start_byte, pattern, full_bytes);
    }

    // Handle any left over pixels which do not consume a full byte
    uint8_t end_outset = remaining_pixels % CHAR_BIT;
    if (end_outset) {
      // Clear the pixels to be written to 0
      bitmap_[start_byte + full_bytes] &= kMask[end_outset];
      // Draw the masked pattern
      bitmap_[start_byte + full_bytes] |= (~kMask[end_outset] & pattern);
      LOG_IF(INFO, kVerbose) << "End outset: " << (int)end_outset
                             << " Mask: " << std::bitset<8>(~kMask[end_outset]);
    }
  }

  SDL_Surface* const as_surface() const {
    static SDL_Surface* const surface = SDL_CreateRGBSurfaceWithFormat(
        SDL_SWSURFACE, kScreenWidth, kScreenHeight, 1,
        SDL_PIXELFORMAT_INDEX1MSB);
    surface->pixels = bitmap_;

    static SDL_Color colors[2] = {kOffColor, kOnColor};
    SDL_SetPaletteColors(surface->format->palette, colors, 0, 2);
    return surface;
  }

  void FillRect(const Rect& rect, const uint8_t pattern[8]) {
    for (int16_t row = rect.top; row < rect.bottom; ++row) {
      FillRow(row, rect.left, rect.right, pattern[row % 8]);
    }
  }

  void FillEllipse(const Rect& rect, const uint8_t pattern[8]) {
    int half_width = (rect.right - rect.left) / 2;
    int half_height = (rect.bottom - rect.top) / 2;
    int origin_x = rect.left + half_width;
    int origin_y = rect.top + half_height;

    // Pre-calculate squares for formula below
    int hh = half_height * half_height;
    int ww = half_width * half_width;
    int hhww = hh * ww;

    int last_offset = half_width;
    int slope_dx = 0;

    // Fill the horrizontal center row of the ellipse.
    // The rest of the ellipse is mirrored over this central line.
    FillRow(origin_y, rect.left, rect.right, pattern[half_height % 8]);

    for (int row = 1; row <= half_height; ++row) {
      // Calculate the new offset from the vertical center for each row
      // exploiting the fact that each new row will differ from the last
      // by at least the same slope line as the last (give or take 1 to
      // account for integer math).
      int offset = last_offset - (slope_dx - 1);
      for (; offset > 0; --offset) {
        if (offset * offset * hh + row * row * ww <= hhww)
          break;
      }

      // Fill rows mirrored over center line taking care to ensure the
      // fill pattern starts at the top of the ellipse and follows down.
      FillRow(origin_y - row, origin_x - offset, origin_x + offset,
              pattern[(half_height - row) % 8]);
      FillRow(origin_y + row, origin_x - offset, origin_x + offset,
              pattern[(half_height + row) % 8]);

      slope_dx = last_offset - offset;
      last_offset = offset;
    }
  }

  void PrintBitmap() const {
    for (size_t i = 0; i < bitmap_size_; ++i) {
      std::cout << std::bitset<8>(bitmap_[i]);
      if ((i + 1) % PixelWidthToBytes(width_) == 0) {
        std::cout << std::endl;
      }
    }
  }

 private:
  const size_t width_;
  const size_t height_;
  const size_t bitmap_size_;
  uint8_t* const bitmap_;
};

Rect NewRect(int16_t x, int16_t y, int16_t width, int16_t height) {
  Rect rect;
  rect.top = y;
  rect.bottom = y + height;
  rect.left = x;
  rect.right = x + width;
  return rect;
}

}  // namespace

absl::Status Main(const core::Args& args) {
  SDL_Init(SDL_INIT_VIDEO);

  int window_width = int(kScreenWidth * kScaleFactor);
  int window_height = int(kScreenHeight * kScaleFactor);

  SDL_Window* const window =
      SDL_CreateWindow("Screen", SDL_WINDOWPOS_UNDEFINED,
                       SDL_WINDOWPOS_UNDEFINED, window_width, window_height, 0);

  SDL_Renderer* const renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, 0);

  BitmapScreen screen(kScreenWidth, kScreenHeight);

  auto fill_rect = NewRect(0, 0, kScreenWidth, kScreenHeight);
  screen.FillRect(fill_rect, kGrey);

  auto window_rect = NewRect(60, 60, 60, 60);
  screen.FillRect(window_rect, kWhite);
  screen.FillEllipse(window_rect, kBlack);

  bool should_exit = false;
  bool is_drag = false;
  int drag_offset_x = 0;
  int drag_offset_y = 0;

  auto within_rect = [](Rect rect, Sint32 x, Sint32 y) {
    return x >= int(rect.left * kScaleFactor) &&
           x <= int(rect.right * kScaleFactor) &&
           y >= int(rect.top * kScaleFactor) &&
           y <= int(rect.bottom * kScaleFactor);
  };

  auto drag_rect = [&](Rect& rect, Sint32 x, Sint32 y) {
    size_t width = rect.right - rect.left;
    size_t height = rect.bottom - rect.top;
    x = (x / kScaleFactor) - drag_offset_x;
    y = (y / kScaleFactor) - drag_offset_y;

    x = x < 0 ? x = 0 : x > kScreenWidth - width ? kScreenWidth - width : x;
    y = y < 0 ? y = 0 : y > kScreenHeight - height ? kScreenHeight - height : y;

    rect = NewRect(x, y, width, height);
  };

  SDL_Event event;
  while (!should_exit) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(renderer, screen.as_surface());
    if (texture == NULL)
      LOG(FATAL) << SDL_GetError();

    static SDL_Rect rect = {
        .x = 0,
        .y = 0,
        .w = window_width,
        .h = window_height,
    };
    SDL_RenderCopy(renderer, texture, NULL, &rect);
    SDL_DestroyTexture(texture);

    SDL_RenderPresent(renderer);

    while (SDL_PollEvent(&event)) {
      switch (event.type) {
        case SDL_QUIT:
          should_exit = true;
          break;
        case SDL_MOUSEBUTTONDOWN:
          if (within_rect(window_rect, event.button.x, event.button.y)) {
            LOG(INFO) << "From: " << window_rect;
            drag_offset_x = (event.button.x / kScaleFactor) - window_rect.left;
            drag_offset_y = (event.button.y / kScaleFactor) - window_rect.top;
            is_drag = true;
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (is_drag) {
            drag_rect(window_rect, event.button.x, event.button.y);

            screen.FillRect(fill_rect, kGrey);
            screen.FillRect(window_rect, kWhite);
            screen.FillEllipse(window_rect, kBlack);
            LOG(INFO) << "To: " << window_rect;
          }
          is_drag = false;
          break;
        case SDL_MOUSEMOTION:
          if (is_drag) {
            drag_rect(window_rect, event.motion.x, event.motion.y);

            screen.FillRect(fill_rect, kGrey);
            screen.FillEllipse(window_rect, kWhite);
          }
          break;
      }
    }
  }

  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return absl::OkStatus();
}
