// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "emu/mouse_listener.h"

namespace cyder {

class NativeBridge {
 public:
  void StartNativeMouseControl(MouseListener* listener);

  void OnMouseMove(int x, int y) const;
  bool OnMouseUp(int x, int y);

 private:
  MouseListener* current_listener_{nullptr};
};

}  // namespace cyder
