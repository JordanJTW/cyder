#include <cstdio>
#include <iostream>
#include <string>
#include <endian.h>

#include "rsrcloader.h"
#include "status_macros.h"

using namespace rsrcloader;

absl::Status StatusMain(const std::string& filename) {    
    ASSIGN_OR_RETURN(auto file, ResourceFile::Load(filename));
    std::cout << *file;
    return absl::OkStatus();
}

int main(int argc, const char** argv) {
    auto status = StatusMain(argv[1]);
    if (!status.ok()) {
        printf("Error: %s\n", std::string(status.message()).c_str());
        return -1;
    }
    return 0;
}