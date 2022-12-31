// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "emu/graphics/grafport_types.tdef.h"

namespace cyder {
namespace port {

absl::StatusOr<Ptr> GetQDGlobalsPtr();
absl::StatusOr<QDGlobals> GetQDGlobals();

absl::StatusOr<Ptr> GetThePort();
absl::Status SetThePort(Ptr port);

absl::StatusOr<Point> GetLocalToGlobalOffset();
absl::StatusOr<Rect> ConvertLocalToGlobal(Rect rect);

Rect LocalToGlobal(const GrafPort& port, const Rect& rect);

}  // namespace port
}  // namespace cyder
