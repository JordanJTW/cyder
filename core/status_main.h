// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#pragma once

#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace core {

// Holds arguments remaining after flags have been parsed by
// absl::ParseCommandLine() and allows safe access
class Args {
 public:
  Args(const std::vector<char*>);

  absl::StatusOr<std::string> GetArg(size_t index,
                                     const std::string& argument_name) const;

 private:
  const std::vector<char*> args_;
};

}  // namespace core

// Prototype definition for the new main entry-point
absl::Status Main(const core::Args& args);
