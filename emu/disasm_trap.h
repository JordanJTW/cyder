#pragma once

#include <cstdint>

#include "third_party/abseil-cpp/absl/strings/string_view.h"

absl::string_view GetTrapName(uint16_t trap);