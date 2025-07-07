
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "third_party/musashi/src/m68k.h"

const core::MemoryRegion* data;

unsigned int m68k_read_disassembler_8(unsigned int address) {
  return MUST(data->Read<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return MUST(data->Read<uint16_t>(address));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return MUST(data->Read<uint32_t>(address));
}

absl::Status Main(const core::Args& args) {
  std::string path = TRY(args.GetArg(1, "FILENAME"));

  std::ifstream file(path, std::ios::in | std::ios::binary);
  if (!file.is_open()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Could not open file: ", path));
  }

  file.seekg(0, std::ios::end);
  std::streamsize size = file.tellg();
  file.seekg(0, std::ios::beg);

  std::vector<char> buffer(size);
  file.read(buffer.data(), size);

  core::MemoryRegion region(buffer.data(), size);
  data = &region;

  if (!file) {
    return absl::InternalError(
        absl::StrCat("Error reading file: ", path, " - ", strerror(errno)));
  }

  size_t pc = 0;  // Program counter, starting at the beginning of the file
  while (pc < size) {
    char disassembly[255];
    size_t increment = m68k_disassemble(disassembly, pc, M68K_CPU_TYPE_68000);

    for (size_t i = 0; i < increment; ++i) {
      printf("0x%02X, ", buffer[pc + i] & 0xFF);
    }
    printf("  // %s\n", disassembly);

    pc += increment;
  }

  return absl::OkStatus();
}