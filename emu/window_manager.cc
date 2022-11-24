#include "emu/window_manager.h"

#include "emu/graphics/graphics_helpers.h"
#include "emu/memory/memory_map.h"

using ::cyder::graphics::BitmapScreen;

namespace cyder {
namespace {

constexpr uint8_t kGreyPattern[8] = {0xAA, 0x55, 0xAA, 0x55,
                                     0xAA, 0x55, 0xAA, 0x55};

}  // namespace

WindowManager::WindowManager(NativeBridge& native_bridge,
                             EventManager& event_manager,
                             BitmapScreen& screen)
    : native_bridge_(native_bridge),
      event_manager_(event_manager),
      screen_(screen) {}

void WindowManager::NativeDragWindow(Ptr window_ptr, int x, int y) {
  native_bridge_.StartNativeMouseControl(this);
  target_window_ptr_ = window_ptr;

  target_window_ =
      MUST(ReadType<WindowRecord>(memory::kSystemMemory, target_window_ptr_));
}

void WindowManager::OnMouseMove(int x, int y) {
  LOG(INFO) << "Move to: (x: " << x << ", y: " << y << ")";
}
void WindowManager::OnMouseUp(int x, int y) {
  if (!target_window_ptr_)
    return;

  screen_.FillRect(MoveRect(target_window_.port.port_rect,
                            -target_window_.port.port_bits.bounds.left,
                            -target_window_.port.port_bits.bounds.top),
                   kGreyPattern);

  target_window_.port.port_bits.bounds.left = -x;
  target_window_.port.port_bits.bounds.top = -y;

  CHECK(WriteType<WindowRecord>(target_window_, memory::kSystemMemory,
                                target_window_ptr_)
            .ok());

  event_manager_.QueueWindowUpdate(target_window_ptr_);
  target_window_ptr_ = 0;
}

}  // namespace cyder
