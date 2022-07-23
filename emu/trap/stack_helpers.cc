#include "stack_helpers.h"

#include "core/endian_helpers.h"

namespace cyder {
namespace trap {

template <>
absl::StatusOr<absl::string_view> PopRef() {
  auto ptr = TRY(Pop<Ptr>());
  auto length = TRY(memory::kSystemMemory.Copy<uint8_t>(ptr));
  return absl::string_view(
      reinterpret_cast<const char*>(memory::kSystemMemory.raw_ptr()) + ptr + 1,
      length);
}

template <>
absl::StatusOr<Rect> PopRef() {
  auto ptr = TRY(Pop<Ptr>());
  Rect rect;
  rect.top = be16toh(TRY(memory::kSystemMemory.Copy<Integer>(ptr)));
  rect.left = be16toh(TRY(memory::kSystemMemory.Copy<Integer>(ptr + 2)));
  rect.bottom = be16toh(TRY(memory::kSystemMemory.Copy<Integer>(ptr + 4)));
  rect.right = be16toh(TRY(memory::kSystemMemory.Copy<Integer>(ptr + 6)));
  return rect;
}

}  // namespace trap
}  // namespace cyder