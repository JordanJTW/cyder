#pragma once

#include "emu/mouse_listener.h"

namespace cyder {

class NativeBridge : public MouseListener {
 public:
  void StartNativeMouseControl(MouseListener* listener);

  // cyder::MouseListener implementation:
  void OnMouseMove(int x, int y) override;
  void OnMouseUp(int x, int y) override;

 private:
  MouseListener* current_listener_ {nullptr};
};

}  // namespace cyder
