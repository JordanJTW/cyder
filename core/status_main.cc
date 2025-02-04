// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "status_main.h"

#include "absl/flags/parse.h"
#include "absl/strings/str_cat.h"
#include "core/logging.h"

namespace core {

Args::Args(const std::vector<char*> args) : args_(std::move(args)) {}

absl::StatusOr<std::string> Args::GetArg(
    size_t index,
    const std::string& argument_name) const {
  if (index >= args_.size()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Missing '", argument_name, "' at position ", index));
  }
  return args_[index];
}

}  // namespace core

int main(int argc, char** argv) {
  auto args = core::Args(absl::ParseCommandLine(argc, argv));

  auto status = Main(args);
  if (!status.ok()) {
    LOG(ERROR) << "Error: " << status.message();
    return -1;
  }
  return 0;
}