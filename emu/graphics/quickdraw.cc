#include "emu/graphics/quickdraw.h"

#include "core/logging.h"
#include "core/status_helpers.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/memory/memory_map.h"
#include "third_party/musashi/src/m68k.h"

namespace cyder {
namespace port {

absl::StatusOr<Ptr> GetQDGlobalsPtr() {
  auto a5_world = m68k_get_reg(/*context=*/NULL, M68K_REG_A5);
  return TRY(memory::kSystemMemory.Read<Ptr>(a5_world));
}

absl::StatusOr<QDGlobals> GetQDGlobals() {
  // The pointer stored at A5 points to the |thePort| field of the QuickDraw
  // globals with them extending beneath it... so offset by the size of the
  // globals in memory accounting for |thePort| offset already counted.
  auto the_port = TRY(GetQDGlobalsPtr());
  return TRY(ReadType<QDGlobals>(
      memory::kSystemMemory, the_port - QDGlobals::fixed_size + sizeof(Ptr)));
}

absl::StatusOr<Ptr> GetThePort() {
  // The pointer to the QuickDraw globals points to the first element (thePort)
  auto the_port = TRY(GetQDGlobalsPtr());
  return TRY(memory::kSystemMemory.Read<Ptr>(the_port));
}

absl::Status SetThePort(Ptr port) {
  // The pointer to the QuickDraw globals points to the first element (thePort)
  auto the_port = TRY(GetQDGlobalsPtr());
  return memory::kSystemMemory.Write<Ptr>(the_port, port);
}

absl::StatusOr<Point> GetLocalToGlobalOffset() {
  auto the_port = TRY(GetThePort());

  auto current_port = TRY(ReadType<GrafPort>(memory::kSystemMemory, the_port));

  Point point;
  point.x = -current_port.port_bits.bounds.left;
  point.y = -current_port.port_bits.bounds.top;
  return point;
}

absl::StatusOr<Rect> ConvertLocalToGlobal(Rect rect) {
  auto offset = TRY(GetLocalToGlobalOffset());
  return OffsetRect(rect, offset.x, offset.y);
}

Rect LocalToGlobal(const GrafPort& port, const Rect& rect) {
  return OffsetRect(rect, -port.port_bits.bounds.left,
                    -port.port_bits.bounds.top);
}

}  // namespace port
}  // namespace cyder
