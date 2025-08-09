// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include <SDL.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#ifdef __LINUX__
#include <signal.h>
#endif  // __LINUX__

#include <atomic>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <regex>
#include <thread>

#include "absl/flags/flag.h"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/debug/debug_manager.h"
#include "emu/debug/debugger.h"
#include "emu/debug_logger.h"
#include "emu/emulator.h"
#include "emu/event_manager.h"
#include "emu/graphics/bitmap_image.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/memory/memory_manager.h"
#include "emu/memory/memory_map.h"
#include "emu/menu_manager.h"
#include "emu/rsrc/resource_file.h"
#include "emu/rsrc/resource_manager.h"
#include "emu/segment_loader.h"
#include "emu/trap/stack_helpers.h"
#include "emu/trap/trap_dispatcher.h"
#include "emu/trap/trap_manager.h"
#include "emu/window_manager.h"
#include "gen/global_names.h"
#include "gen/trap_names.h"
#include "third_party/musashi/src/m68k.h"

ABSL_FLAG(std::string,
          system_file,
          /*default_value=*/"",
          "A Macintosh System (1-5) resource file to pull from");

ABSL_FLAG(bool,
          headless,
          /*default_value=*/false,
          "Runs Cyder without a window");

ABSL_FLAG(bool,
          exit_on_idle,
          /*default_value=*/false,
          "Save a screenshot and exit once idle (use with --headless)");

ABSL_FLAG(bool,
          debugger,
          /*default_value=*/false,
          "Enables the Cyder debugger prompt");

#define SHOW_WINDOW

constexpr SDL_Color kOnColor = {0xFF, 0xFF, 0xFF, 0xFF};
constexpr SDL_Color kOffColor = {0x00, 0x00, 0x00, 0xFF};

constexpr size_t kScreenWidth = 512;
constexpr size_t kScreenHeight = 384;
constexpr size_t kScaleFactor = 1;

constexpr uint8_t kGreyPattern[8] = {0xAA, 0x55, 0xAA, 0x55,
                                     0xAA, 0x55, 0xAA, 0x55};

// unsigned int m68k_read_memory_16(unsigned int address) {
//   cyder::memory::CheckReadAccess(address);

//   if (address >= cyder::memory::kLastEmulatedSubroutineAddress &&
//       address < cyder::memory::kSystemMemorySize) {
//     CHECK(on_emulated_subroutine)
//         << "No emulated subroutine callback registered";
//     std::optional<Trap> trap = on_emulated_subroutine(address);
//     if (trap.has_value()) {
//       if ((Debugger::Instance().ShouldBreakOnSystemTaskTrap() &&
//            trap.value() == Trap::SystemTask) ||
//           (Debugger::Instance().ShouldBreakOnExitTrap() &&
//            trap.value() == Trap::SysBeep)) {
//         m68k_end_timeslice();
//       }
//     }
//   }

//   return MUST(cyder::memory::kSystemMemory.Read<uint16_t>(address));
// }

static ::cyder::DebugLogger logger;

// void cpu_instr_callback(unsigned int pc) {
//   CHECK(pc != 0) << "Reset";

//   CHECK(m68k_get_reg(NULL, M68K_REG_ISP) <= cyder::memory::kStackStart);
//   CHECK(m68k_get_reg(NULL, M68K_REG_ISP) > cyder::memory::kStackEnd);

//   auto instr = MUST(cyder::memory::kSystemMemory.Read<uint16_t>(pc));
//   if ((instr & 0xFFC0) == 0x4E80) {
//     // Is JSR instruction per:
//     http://goldencrystal.free.fr/M68kOpcodes-v2.3.pdf
//     // TODO: Track subroutine calls/returns for debugging?
//   }

//   if (Debugger::Instance().IsSingleStep()) {
//     char buffer[255];
//     m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
//     printf("0x%x: %s\n", pc, buffer);
//     m68k_end_timeslice();
//   }
// }

void PrintFrameTiming(std::ostream& os = std::cout, float period = 2.0f) {
  static unsigned int frames = 0;
  frames++;
  static auto start = std::chrono::steady_clock::now();
  auto end = std::chrono::steady_clock::now();

  float seconds =
      std::chrono::duration_cast<std::chrono::duration<float>>(end - start)
          .count();
  if (seconds > period) {
    float spf = seconds / frames;
    os << frames << " frames in " << std::setprecision(1) << std::fixed
       << seconds << " seconds = " << std::setprecision(1) << std::fixed
       << 1.0f / spf << " FPS (" << std::setprecision(3) << std::fixed
       << spf * 1000.0f << " ms/frame)\n";
    frames = 0;
    start = end;
  }
}

using cyder::EventManager;
using cyder::MenuManager;
using cyder::NewRect;
using cyder::ResourceManager;
using cyder::SegmentLoaderImpl;
using cyder::WindowManager;
using cyder::graphics::BitmapImage;
using cyder::memory::kSystemMemory;
using cyder::memory::MemoryManager;
using cyder::rsrc::ResourceFile;
using cyder::trap::TrapDispatcherImpl;
using cyder::trap::TrapManager;

SDL_Surface* const MakeSurface(const BitmapImage& screen) {
  static SDL_Surface* const surface = SDL_CreateRGBSurfaceWithFormat(
      SDL_SWSURFACE, kScreenWidth, kScreenHeight, 1, SDL_PIXELFORMAT_INDEX1MSB);
  surface->pixels = const_cast<uint8_t*>(screen.bits());

  static SDL_Color colors[2] = {kOnColor, kOffColor};
  SDL_SetPaletteColors(surface->format->palette, colors, 0, 2);
  return surface;
}

absl::Status UpdateGlobalTime() {
  return kSystemMemory.Write<uint32_t>(
      GlobalVars::Time, absl::ToUnixSeconds(absl::Now()) + 2082844800);
}

void SaveScreenshot(const BitmapImage& screen) {
  auto path = absl::StrCat("/tmp/cyder-", absl::ToUnixMillis(absl::Now()));
  screen.SaveBitmap(path);
  LOG(INFO) << "Screenshot saved to: " << path;
}

absl::Status InitializeVM(size_t pc) {
  cyder::Emulator::Instance().Init(pc);

  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(GlobalVars::ApplLimit,
                                                cyder::memory::kHeapEnd));
  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
      GlobalVars::CurrentA5, cyder::memory::GetA5WorldPosition()));

  // Sets the size of the name to 0 so it is not read:
  // TODO: Store the application name here as a Pascal string
  RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(GlobalVars::CurApName, 0));

  // This value was arbitraily chosen based on testing what felt natural.
  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(GlobalVars::DoubleTime, 16));
  // According to documentation "monkey lives if nonzero" but that is not the
  // behavior observed with MacPaint. In MacPaint a value of zero causes
  // "About MacPaint" to never display the dialog due to a MonkeyLives gaurd...
  // I am really not sure what's going on here but can not argue with success...
  // Experimenting in Basillisk II & minivmac yields a constant of 0xFFB8.
  RETURN_IF_ERROR(
      kSystemMemory.Write<uint16_t>(GlobalVars::MonkeyLives, 0xFFB8));

  RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(GlobalVars::ROM85, 0x0000));

  // Write low-memory constants:
  RETURN_IF_ERROR(
      kSystemMemory.Write<uint32_t>(GlobalVars::MinusOne, 0xFFFFFFFF));
  RETURN_IF_ERROR(
      kSystemMemory.Write<uint32_t>(GlobalVars::OneOne, 0x00010001));
  RETURN_IF_ERROR(
      kSystemMemory.Write<uint32_t>(GlobalVars::Lo3Bytes, 0x00FFFFFF));

  RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(GlobalVars::CurStackBase,
                                                cyder::memory::kStackStart));

  cyder::trap::Push<uint32_t>(cyder::memory::kBaseToolboxTrapAddress +
                              (Trap::ExitToShell & 0x03FF) * sizeof(uint16_t));
  return absl::OkStatus();
}

void run_emulator_thread(std::atomic<bool>& is_running) {
  while (is_running.load()) {
    // If `--debugger` was passed continue to prompt the user for commands until
    // `Prompt()` indicates it is ready to run the main emulation loop.
    if (absl::GetFlag(FLAGS_debugger)) {
      while (!cyder::Debugger::Instance().Prompt()) {
        // The thread may have exited while the user was in the prompt.
        if (!is_running.load())
          return;
      }
    }

    CHECK_OK(UpdateGlobalTime());
    cyder::Emulator::Instance().Run();
  }
}

bool main_loop(SDL_Renderer* renderer,
               SDL_Window* window,
               BitmapImage& screen,
               EventManager& event_manager) {
  if (renderer != nullptr) {
    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(renderer, MakeSurface(screen));
    CHECK(texture) << "Failed to create texture: " << SDL_GetError();

    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    SDL_DestroyTexture(texture);
  }

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    switch (event.type) {
      case SDL_KEYDOWN:
        if (event.key.keysym.sym == SDLK_F2) {
          SaveScreenshot(screen);
        } else if (event.key.keysym.sym == SDLK_F10) {
          cyder::Debugger::Instance().Break();
        } else {
          event_manager.QueueKeyDown();
        }
        break;
      case SDL_MOUSEBUTTONDOWN:
        if (window) {
          SDL_SetWindowGrab(window, SDL_TRUE);
        }
        event_manager.QueueMouseDown(event.button.x / kScaleFactor,
                                     event.button.y / kScaleFactor);
        break;
      case SDL_MOUSEMOTION:
        event_manager.OnMouseMove(event.motion.x / kScaleFactor,
                                  event.motion.y / kScaleFactor);
        break;
      case SDL_MOUSEBUTTONUP:
        if (window) {
          SDL_SetWindowGrab(window, SDL_FALSE);
        }
        event_manager.QueueMouseUp(event.button.x / kScaleFactor,
                                   event.button.y / kScaleFactor);
        break;
      case SDL_QUIT:
#ifdef __EMSCRIPTEN__
        emscripten_cancel_main_loop();
#endif
        return false;  // Exit the main loop.
    }
  }
  if (!(absl::GetFlag(FLAGS_headless) || absl::GetFlag(FLAGS_debugger))) {
    // PrintFrameTiming();
  }
  return true;  // Continue the main loop.
}

struct MainLoopArgs {
  SDL_Renderer* renderer;
  SDL_Window* window;
  BitmapImage* screen;
  EventManager* event_manager;
};

void run_main_loop(void* args) {
  MainLoopArgs* main_loop_args = static_cast<MainLoopArgs*>(args);
  main_loop(main_loop_args->renderer, main_loop_args->window,
            *main_loop_args->screen, *main_loop_args->event_manager);
}

absl::Status Main(const core::Args& args) {
  auto file = TRY(ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));

  auto system_path = absl::GetFlag(FLAGS_system_file);
  std::unique_ptr<cyder::rsrc::ResourceFile> system_file;
  if (!system_path.empty()) {
    system_file = TRY(ResourceFile::Load(system_path));
  }

  cyder::DebugManager::Instance().TagMemory(
      cyder::memory::kStackEnd, cyder::memory::kStackStart, "Stack");
  MemoryManager memory_manager;
  logger.SetMemoryManager(&memory_manager);
  cyder::memory::InstallMemoryWatcher();

  ResourceManager resource_manager(memory_manager, *file, system_file.get());

  auto segment_loader =
      TRY(SegmentLoaderImpl::Create(memory_manager, resource_manager));

  size_t pc = TRY(segment_loader->Load(1));
  LOG(INFO) << "Initialize PC: " << std::hex << pc;
  LOG(INFO) << "Memory Map: " << cyder::memory::MemoryMapToStr();

  RETURN_IF_ERROR(InitializeVM(pc));

  BitMap bitmap;
  bitmap.bounds = NewRect(0, 0, kScreenWidth, kScreenHeight);
  bitmap.row_bytes = cyder::PixelWidthToBytes(kScreenWidth);
  bitmap.base_addr = memory_manager.Allocate(bitmap.row_bytes * kScreenHeight);

  RETURN_IF_ERROR(
      kSystemMemory.Write<uint16_t>(GlobalVars::ScreenRow, bitmap.row_bytes));

  BitmapImage screen(bitmap,
                     kSystemMemory.raw_mutable_ptr() + bitmap.base_addr);

  MenuManager menu_manager(screen);

  // Draw classic Mac OS grey baground pattern
  auto screen_rect = NewRect(0, 0, kScreenWidth, kScreenHeight);
  screen.FillRect(screen_rect, kGreyPattern);

  menu_manager.DrawMenuBar();

  SDL_Init(SDL_INIT_VIDEO);

  int window_width = int(kScreenWidth * kScaleFactor);
  int window_height = int(kScreenHeight * kScaleFactor);

  SDL_Window* window = nullptr;
  SDL_Renderer* renderer = nullptr;

  if (!absl::GetFlag(FLAGS_headless)) {
    window = SDL_CreateWindow("Cyder", SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, window_width,
                              window_height, SDL_WINDOW_ALLOW_HIGHDPI);
    CHECK(window) << "Failing to create window: " << SDL_GetError();

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC);
    CHECK(renderer) << "Failing to create renderer: " << SDL_GetError();
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, 0);

  EventManager event_manager;
  WindowManager window_manager(event_manager, screen, memory_manager);
  TrapDispatcherImpl trap_dispatcher(memory_manager, resource_manager,
                                     event_manager, menu_manager,
                                     window_manager, bitmap);
  TrapManager trap_manager(*segment_loader, trap_dispatcher);

  if (system_file) {
    trap_manager.PatchTrapsFromSystemFile(memory_manager, *system_file);
  }

  std::atomic<bool> is_emulator_running{true};
  std::thread emulator_thread(run_emulator_thread,
                              std::ref(is_emulator_running));

#ifdef __EMSCRIPTEN__
  MainLoopArgs main_loop_args{renderer, window, &screen, &event_manager};
  emscripten_set_main_loop_arg(run_main_loop, &main_loop_args, 0, 1);
#else
  while (main_loop(renderer, window, screen, event_manager)) {
    SDL_Delay(0);
  };
#endif

  if (!absl::GetFlag(FLAGS_headless)) {
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
  }

  // Prepare the emulator thread to shutdown.
  is_emulator_running.store(false);
#ifndef __EMSCRIPTEN__
  // Send SIGINT to interrupt any console I/O the thread may be blocked on.
  pthread_kill(emulator_thread.native_handle(), SIGINT);
#endif  // __EMSCRIPTEN__
  // Shutdown the EventManager to notify any CondVars it may be blocked on.
  event_manager.Shutdown();
  // Attempt to join the thread.
  emulator_thread.join();
  SDL_Quit();
  return absl::OkStatus();
}
