#include "emu/graphics/quickdraw.h"

#include "core/logging.h"
#include "core/status_helpers.h"
#include "emu/memory/memory_map.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace port {

absl::StatusOr<Ptr> GetQDGlobals() {
  auto a5_world = m68k_get_reg(/*context=*/NULL, M68K_REG_A5);
  return TRY(memory::kSystemMemory.Read<Ptr>(a5_world));
}

absl::StatusOr<Ptr> GetThePort() {
  // The pointer to the QuickDraw globals points to the first element (thePort)
  auto the_port = TRY(GetQDGlobals());
  return TRY(memory::kSystemMemory.Read<Ptr>(the_port));
}

absl::Status SetThePort(Ptr port) {
  // The pointer to the QuickDraw globals points to the first element (thePort)
  auto the_port = TRY(GetQDGlobals());
  return memory::kSystemMemory.Write<Ptr>(the_port, port);
}

absl::StatusOr<Point> GetLocalToGlobalOffset() {
  auto the_port = TRY(GetThePort());

  auto current_port = TRY(ReadType<GrafPort>(memory::kSystemMemory, the_port));

  Point point;
  point.x = current_port.port_rect.left;
  point.y = current_port.port_rect.top;
  return point;
}

}  // namespace port
}  // namespace cyder
