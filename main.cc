#include <cstdio>
#include <iostream>
#include <string>

#include "core/logging.h"
#include "core/status_helpers.h"
#include "resource_file.h"

using namespace rsrcloader;

absl::Status StatusMain(const std::string& filename) {
  auto file = TRY(ResourceFile::Load(filename));
  RETURN_IF_ERROR(file->Save("/tmp/output.rsrc"));
  return absl::OkStatus();
}

int main(int argc, const char** argv) {
  auto status = StatusMain(argv[1]);
  if (!status.ok()) {
    LOG(ERROR) << "Error: " << status.message();
    return -1;
  }
  return 0;
}