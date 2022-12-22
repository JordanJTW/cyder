#include <gmock/gmock-matchers.h>
#include <gtest/gtest.h>

#include "absl/status/statusor.h"
#include "core/status_helpers.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/memory/memory_map.h"

namespace cyder {
namespace memory {

TEST(MemoryMapTests, RestrictAccessReadFatal) {
  CHECK(WriteType<Rect>(Rect{0, 0, 20, 20}, kSystemMemory, kHeapStart).ok());
  RESTRICT_FIELD_ACCESS(Rect, kHeapStart);

  EXPECT_DEATH(CheckReadAccess(kHeapStart), "Read within protected");
}

TEST(MemoryMapTests, RestrictAccessWriteFatal) {
  CHECK(WriteType<Rect>(Rect{0, 0, 20, 20}, kSystemMemory, kHeapStart).ok());
  RESTRICT_FIELD_ACCESS(Rect, kHeapStart);

  EXPECT_DEATH(CheckWriteAccess(kHeapStart, 0), "Write within protected");
}

TEST(MemoryMapTests, RestrictAccessAllowed) {
  CHECK(WriteType<Rect>(Rect{0, 0, 20, 20}, kSystemMemory, kHeapStart).ok());
  RESTRICT_FIELD_ACCESS(Rect, kHeapStart, RectFields::top);

  CheckReadAccess(kHeapStart);
  CheckWriteAccess(kHeapStart, 0);
}

TEST(MemoryMapTests, RestrictAccessOverride) {
  CHECK(WriteType<Rect>(Rect{0, 0, 20, 20}, kSystemMemory, kHeapStart).ok());
  RESTRICT_FIELD_ACCESS(Rect, kHeapStart);

  // Override the previously set RESTRICT_FIELD_ACCESS
  RESTRICT_FIELD_ACCESS(Rect, kHeapStart, RectFields::top);

  CheckReadAccess(kHeapStart);
  CheckWriteAccess(kHeapStart, 0);
}

TEST(MemoryMapTests, RestrictAccessOverlapping) {
  CHECK(WriteType<Rect>(Rect{0, 0, 20, 20}, kSystemMemory, kHeapStart).ok());
  RESTRICT_FIELD_ACCESS(Rect, kHeapStart);

  CHECK(WriteType<Rect>(Rect{20, 20, 40, 40}, kSystemMemory, kHeapStart + 4)
            .ok());
  RESTRICT_FIELD_ACCESS(Rect, kHeapStart, RectFields::top);

  CheckReadAccess(kHeapStart);
  CheckWriteAccess(kHeapStart, 0);
}

}  // namespace memory
}  // namespace cyder
