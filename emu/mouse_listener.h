// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "emu/graphics/grafport_types.tdef.h"

namespace cyder {

class MouseListener {
 public:
  virtual ~MouseListener() = default;

  virtual void OnMouseMove(const Point&) = 0;
  virtual void OnMouseUp(const Point&) = 0;
};

}  // namespace cyder
