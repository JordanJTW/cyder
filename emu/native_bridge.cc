#include "emu/native_bridge.h"

#include "core/logging.h"
#include "third_party/musashi/src/m68k.h"

extern bool single_step;

namespace cyder {

void NativeBridge::StartNativeMouseControl(MouseListener* listener) {
  m68k_end_timeslice();
  single_step = true;
  current_listener_ = listener;
}

void NativeBridge::OnMouseMove(int x, int y) const {
  if (current_listener_) {
    // TODO: Ensure safe narrowing conversions?
    current_listener_->OnMouseMove(
        {.y = static_cast<int16_t>(y), .x = static_cast<int16_t>(x)});
  }
}

bool NativeBridge::OnMouseUp(int x, int y) {
  if (!current_listener_) {
    return false;
  }

  // TODO: Ensure safe narrowing conversions?
  current_listener_->OnMouseUp(
      {.y = static_cast<int16_t>(y), .x = static_cast<int16_t>(x)});
  current_listener_ = nullptr;
  single_step = false;
  return true;
}

}  // namespace cyder
