// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "core/memory_region.h"
#include "emu/graphics/grafport_types.tdef.h"

namespace cyder {
namespace graphics {

absl::StatusOr<Rect> GetPICTFrame(const core::MemoryRegion& region);

absl::Status ParsePICTv1(const core::MemoryRegion& region, uint8_t* output);

}  // namespace graphics
}  // namespace cyder