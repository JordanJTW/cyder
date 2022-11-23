#pragma once

namespace cyder {

class MouseListener {
 public:
  virtual ~MouseListener() = default;

  virtual void OnMouseMove(int x, int y) = 0;
  virtual void OnMouseUp(int x, int y) = 0;
};

}  // namespace cyder
