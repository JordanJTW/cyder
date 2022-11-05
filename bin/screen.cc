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
        bitmap_[start_byte] |=
            (kMask[start_offset] & ~kMask[byte_aligned_size] & pattern);
        return;
      }

      LOG_IF(INFO, kVerbose)
          << "Start inset: " << (int)start_offset
          << " Mask: " << std::bitset<8>(kMask[start_offset]);

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

  void FillRect(const Rect& rect, uint8_t pattern[8]) {
    for (int16_t row = rect.top; row < rect.bottom; ++row) {
      FillRow(row, rect.left, rect.right, pattern[row % 8]);
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

  Rect fill_rect;
  fill_rect.top = 0;
  fill_rect.bottom = kScreenHeight;
  fill_rect.left = 0;
  fill_rect.right = kScreenWidth;

  uint8_t pattern[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};

  screen.FillRect(fill_rect, pattern);

  uint8_t black[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  uint8_t white[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

  Rect window_rect;
  window_rect.top = 60;
  window_rect.left = 60;
  window_rect.bottom = 120;
  window_rect.right = 120;
  screen.FillRect(window_rect, white);

  bool should_exit = false;
  bool is_drag = false;

  auto within_rect = [](Rect rect, Sint32 x, Sint32 y) {
    return x >= int(rect.left * kScaleFactor) &&
           x <= int(rect.right * kScaleFactor) &&
           y >= int(rect.top * kScaleFactor) &&
           y <= int(rect.bottom * kScaleFactor);
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
            is_drag = true;
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (is_drag) {
            size_t width = window_rect.right - window_rect.left;
            size_t height = window_rect.bottom - window_rect.top;
            window_rect.top = event.button.y / kScaleFactor;
            window_rect.left = event.button.x / kScaleFactor;
            window_rect.bottom = event.button.y / kScaleFactor + height;
            window_rect.right = event.button.x / kScaleFactor + width;
            screen.FillRect(fill_rect, pattern);
            screen.FillRect(window_rect, white);
            LOG(INFO) << "To: " << window_rect;
          }
          is_drag = false;
          break;
        case SDL_MOUSEMOTION:
          if (is_drag) {
            size_t width = window_rect.right - window_rect.left;
            size_t height = window_rect.bottom - window_rect.top;
            window_rect.top = event.motion.y / kScaleFactor;
            window_rect.left = event.motion.x / kScaleFactor;
            window_rect.bottom = event.motion.y / kScaleFactor + height;
            window_rect.right = event.motion.x / kScaleFactor + width;
            screen.FillRect(fill_rect, pattern);
            screen.FillRect(window_rect, white);
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
