// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <SDL.h>

#include "absl/strings/str_cat.h"
#include "core/logging.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/graphics/bitmap_image.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/pict_v1.h"
#include "emu/graphics/region.h"

namespace {

using ::cyder::FrameRectToBytes;
using ::cyder::NewRect;
using ::cyder::OffsetRect;
using ::cyder::RectHeight;
using ::cyder::RectWidth;
using ::cyder::graphics::BitmapImage;
using ::cyder::graphics::GetPICTFrame;
using ::cyder::graphics::ParsePICTv1;
using ::cyder::region::ConvertRegion;
using ::cyder::region::NewRectRegion;
using ::cyder::region::Subtract;
using ::cyder::region::Union;

constexpr SDL_Color kOnColor = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr SDL_Color kOffColor = {0x00, 0x00, 0x00, 0xFF};

// The Macintosh screen behaves like paper (white background / black foreground)
constexpr uint8_t kWhite[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
constexpr uint8_t kGrey[8] = {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55};
constexpr uint8_t kBlack[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

constexpr int16_t kScreenWidth = 512;
constexpr int16_t kScreenHeight = 384;

constexpr size_t kScaleFactor = 1;

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

absl::Status LoadFileError(absl::string_view file_path) {
  return absl::InternalError(
      absl::StrCat("Error loading: '", file_path, "': ", strerror(errno)));
}

// FIXME: This logic is copy-pasted multiple places; merge into MemoryRegion?
absl::StatusOr<core::MemoryRegion> LoadFile(const std::string& path) {
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
  return core::MemoryRegion(mmap_ptr, size);
}

SDL_Surface* const MakeSurface(const BitmapImage& screen) {
  static SDL_Surface* const surface = SDL_CreateRGBSurfaceWithFormat(
      SDL_SWSURFACE, kScreenWidth, kScreenHeight, 1, SDL_PIXELFORMAT_INDEX1MSB);
  surface->pixels = const_cast<uint8_t*>(screen.bits());

  static SDL_Color colors[2] = {kOnColor, kOffColor};
  SDL_SetPaletteColors(surface->format->palette, colors, 0, 2);
  return surface;
}

}  // namespace

absl::Status Main(const core::Args& args) {
  SDL_Init(SDL_INIT_VIDEO);

  int window_width = int(kScreenWidth * kScaleFactor);
  int window_height = int(kScreenHeight * kScaleFactor);

  SDL_Window* const window = SDL_CreateWindow(
      "Screen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, window_width,
      window_height, SDL_WINDOW_ALLOW_HIGHDPI);

  SDL_Renderer* const renderer =
      SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, 0);

  BitmapImage screen(kScreenWidth, kScreenHeight);

  auto fill_rect = NewRect(0, 0, kScreenWidth, kScreenHeight);
  screen.FillRect(fill_rect, kGrey);

  auto window_rect = NewRect(60, 60, 60, 60);
  screen.FillRect(window_rect, kWhite);
  screen.FillEllipse(window_rect, kBlack);

  auto pict_data = TRY(LoadFile(TRY(args.GetArg(1, "FILENAME"))));
  auto frame = TRY(GetPICTFrame(pict_data));

  size_t picture_size = FrameRectToBytes(frame);
  uint8_t picture[picture_size];
  std::memset(picture, 0, picture_size);

  RETURN_IF_ERROR(ParsePICTv1(pict_data, /*output=*/picture));
  auto picture_rect = NewRect(kScreenWidth - RectWidth(frame),
                              kScreenHeight - RectHeight(frame),
                              RectWidth(frame), RectHeight(frame));
  screen.CopyBits(picture, frame, frame, picture_rect);

  auto region = NewRectRegion(0, 0, 20, 20);
  auto region2 = NewRectRegion(15, 15, 20, 20);
  auto region3 = NewRectRegion(25, 25, 5, 5);

  auto result = Union(ConvertRegion(region, /*is_big_endian=*/false),
                      ConvertRegion(region2, /*is_big_endian=*/false));
  auto final_result = Subtract(ConvertRegion(result, /*is_big_endian=*/false),
                               ConvertRegion(region3, /*is_big_endian=*/false));

  screen.FillRect(final_result.rect, kBlack);
  screen.FillRegion(final_result, kWhite);

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
    int16_t width = RectWidth(rect);
    int16_t height = RectHeight(rect);
    x = (x / kScaleFactor) - drag_offset_x;
    y = (y / kScaleFactor) - drag_offset_y;
    rect = NewRect(x, y, width, height);
  };

  SDL_Event event;
  while (!should_exit) {
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);

    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(renderer, MakeSurface(screen));
    CHECK(texture) << "Failed to create texture: " << SDL_GetError();

    SDL_RenderCopy(renderer, texture, NULL, NULL);
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
        case SDL_MOUSEMOTION:
          if (is_drag) {
            drag_rect(window_rect, event.motion.x, event.motion.y);

            screen.FillRect(fill_rect, kGrey);
            screen.FillEllipse(window_rect, kWhite);
            screen.CopyBits(picture, frame, frame, picture_rect);
          }
          break;
        case SDL_MOUSEBUTTONUP:
          if (is_drag) {
            drag_rect(window_rect, event.button.x, event.button.y);

            screen.FillRect(fill_rect, kGrey);
            screen.FillRect(window_rect, kWhite);
            screen.FillEllipse(window_rect, kBlack);

            screen.FillRect(final_result.rect, kBlack);
            screen.FillRegion(final_result, kWhite);
            screen.CopyBits(picture, frame, frame, picture_rect);
            LOG(INFO) << "To: " << window_rect;
          }
          is_drag = false;
          break;
      }
    }
  }

  // FIXME: Allow MemoryRegion to own memory and properly free it?
  munmap(const_cast<uint8_t*>(pict_data.raw_ptr()), pict_data.size());
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return absl::OkStatus();
}
