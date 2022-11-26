#include "emu/window_manager.h"

#include "emu/graphics/font/basic_font.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/memory/memory_map.h"

using ::cyder::graphics::BitmapScreen;
using ::cyder::memory::MemoryManager;

namespace cyder {
namespace {

constexpr uint8_t kWhitePattern[8] = {0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kGreyPattern[8] = {0xAA, 0x55, 0xAA, 0x55,
                                     0xAA, 0x55, 0xAA, 0x55};

constexpr uint8_t kBlackPattern[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF};

constexpr uint8_t kTitleBarPattern[8] = {0xFF, 0x00, 0xFF, 0x00,
                                         0xFF, 0x00, 0xFF, 0x00};

constexpr uint16_t kFrameTitleHeight = 16u;

}  // namespace

WindowManager::WindowManager(NativeBridge& native_bridge,
                             EventManager& event_manager,
                             BitmapScreen& screen,
                             const MemoryManager& memory)
    : native_bridge_(native_bridge),
      event_manager_(event_manager),
      screen_(screen),
      memory_(memory) {}

void WindowManager::NativeDragWindow(Ptr window_ptr, int x, int y) {
  native_bridge_.StartNativeMouseControl(this);
  target_window_ptr_ = window_ptr;

  target_window_ =
      MUST(ReadType<WindowRecord>(memory::kSystemMemory, target_window_ptr_));

  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(target_window_.structure_region));
  target_offset_.x = struct_region.bounding_box.left - x;
  target_offset_.y = struct_region.bounding_box.top - y + kFrameTitleHeight;

  LOG(INFO) << "Window click offset: " << (x + target_offset_.x) << ", "
            << (y + target_offset_.y);
}

bool WindowManager::CheckIsWindowDrag(Ptr window_ptr, int x, int y) const {
  auto window_record =
      MUST(ReadType<WindowRecord>(memory::kSystemMemory, window_ptr));

  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(window_record.structure_region));

  if (x < struct_region.bounding_box.left ||
      x > struct_region.bounding_box.right ||
      y < struct_region.bounding_box.top ||
      y > struct_region.bounding_box.top + kFrameTitleHeight) {
    return false;
  }
  return true;
}

void WindowManager::OnMouseMove(int x, int y) {
  LOG(INFO) << "Move to: (x: " << x << ", y: " << y << ")";
}
void WindowManager::OnMouseUp(int x, int y) {
  if (!target_window_ptr_)
    return;

  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(target_window_.structure_region));

  screen_.FillRect(struct_region.bounding_box, kGreyPattern);

  target_window_.port.port_bits.bounds.left = -(x + target_offset_.x);
  target_window_.port.port_bits.bounds.top = -(y + target_offset_.y);
  SetStructRegionAndDrawFrame(screen_, target_window_, memory_);

  CHECK(WriteType<WindowRecord>(target_window_, memory::kSystemMemory,
                                target_window_ptr_)
            .ok());

  event_manager_.QueueWindowUpdate(target_window_ptr_);
  target_window_ptr_ = 0;
}

void SetStructRegionAndDrawFrame(BitmapScreen& screen,
                                 WindowRecord& window,
                                 const MemoryManager& memory) {
  constexpr uint16_t kFrameWidth = 1u;

  Rect global_port_rect =
      OffsetRect(window.port.port_rect, -window.port.port_bits.bounds.left,
               -window.port.port_bits.bounds.top);

  Rect frame_rect;
  frame_rect.left = global_port_rect.left - kFrameWidth;
  frame_rect.right = global_port_rect.right + kFrameWidth;
  frame_rect.top = global_port_rect.top - kFrameWidth;
  frame_rect.bottom = global_port_rect.bottom + kFrameWidth;
  screen.FrameRect(frame_rect, kBlackPattern);

  Rect title_bar_rect =
      NewRect(global_port_rect.left - kFrameWidth,
              frame_rect.top - kFrameTitleHeight + kFrameWidth,
              RectWidth(global_port_rect) + kFrameWidth * 2, kFrameTitleHeight);

  screen.FillRect(title_bar_rect, kTitleBarPattern);
  screen.FrameRect(title_bar_rect, kBlackPattern);

  Region region;
  region.bounding_box = UnionRect(frame_rect, title_bar_rect);
  CHECK(memory.WriteTypeToHandle<Region>(region, window.structure_region).ok());

  auto title =
      MUST(memory.ReadTypeFromHandle<std::string>(window.title_handle));

  constexpr uint16_t kTitlePaddingWidth = 4u;
  uint16_t title_width = title.size() * 8;

  Rect title_rect = NewRect((RectWidth(title_bar_rect) - title_width) / 2 +
                                title_bar_rect.left - kTitlePaddingWidth,
                            title_bar_rect.top + kFrameWidth,
                            title_width + kTitlePaddingWidth * 2,
                            kFrameTitleHeight - kFrameWidth * 2);

  screen.FillRect(title_rect, kWhitePattern);
  DrawString(screen, title, title_rect.left + kTitlePaddingWidth,
             title_bar_rect.top + ((kFrameTitleHeight - 8) / 2));
}

}  // namespace cyder
