// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "emu/graphics/bitmap_image.h"
#include "emu/graphics/grafport_types.tdef.h"

namespace cyder {
namespace graphics {

// Creates a BitmapImage tied to the BitMap of `the_port`
BitmapImage PortImageFor(Ptr the_port);

// Creates a BitmapImage tied to the BitMap of the current Port
BitmapImage ThePortImage();

}  // namespace graphics

namespace port {

absl::StatusOr<Ptr> GetQDGlobalsPtr();
absl::StatusOr<QDGlobals> GetQDGlobals();

absl::StatusOr<Ptr> GetThePort();
absl::Status SetThePort(Ptr port);

absl::StatusOr<Point> GetLocalToGlobalOffset();
absl::StatusOr<Rect> ConvertLocalToGlobal(Rect rect);

Rect LocalToGlobal(const GrafPort& port, const Rect& rect);
Point GlobalToLocal(const GrafPort& port, const Point& pt);

void InitPort(GrafPort& port);

}  // namespace port
}  // namespace cyder
