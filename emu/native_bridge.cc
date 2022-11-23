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

void NativeBridge::OnMouseMove(int x, int y) {
  if (current_listener_) {
    current_listener_->OnMouseMove(x, y);
  }
}

void NativeBridge::OnMouseUp(int x, int y) {
  if (current_listener_) {
    current_listener_->OnMouseUp(x, y);
    current_listener_ = nullptr;
    single_step = false;
  }
}

}  // namespace cyder
