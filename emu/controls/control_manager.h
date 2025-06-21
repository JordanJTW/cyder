#pragma once

#include <cstdint>

#include "absl/status/statusor.h"
#include "emu/rsrc/resource_file.h"
#include "emu/window_manager.tdef.h"
#include "gen/typegen/typegen_prelude.h"

namespace cyder {
namespace control {

absl::StatusOr<Handle> GetNewControl(uint16_t control_id, WindowPtr owner);

}  // namespace control
}  // namespace cyder
