#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "emu/graphics/grafport_types.h"

namespace cyder {
namespace port {

absl::StatusOr<Ptr> GetQDGlobals();

absl::StatusOr<Ptr> GetThePort();
absl::Status SetThePort(Ptr port);

}  // namespace port
}  // namespace cyder
