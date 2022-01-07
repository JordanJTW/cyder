#include "absl/status/status.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "resource_file.h"
#include "third_party/musashi/src/m68k.h"

uint8_t s_memory_data[128 * 1024];
core::MemoryRegion s_memory(&s_memory_data, 128 * 1024);

unsigned int m68k_read_disassembler_8(unsigned int address) {
  return MUST(s_memory.Copy<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return be16toh(MUST(s_memory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return be32toh(MUST(s_memory.Copy<uint32_t>(address)));
}

unsigned int m68k_read_memory_8(unsigned int address) {
  return MUST(s_memory.Copy<uint8_t>(address));
}
unsigned int m68k_read_memory_16(unsigned int address) {
  return be16toh(MUST(s_memory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_memory_32(unsigned int address) {
  return be32toh(MUST(s_memory.Copy<uint32_t>(address)));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  MUST(s_memory.Write<uint8_t>(address, value));
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  MUST(s_memory.Write<uint16_t>(address, htobe16(value)));
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  MUST(s_memory.Write<uint32_t>(address, htobe32(value)));
}

void cpu_instr_callback(unsigned int pc) {
  char buffer[255];
  size_t increment = m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
  LOG(INFO) << std::hex << pc << ": " << buffer;
  m68k_end_timeslice();
}

absl::Status Main(const core::Args& args) {
  m68k_init();
  m68k_set_instr_hook_callback(cpu_instr_callback);
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);
  m68k_pulse_reset();

  std::string input;
  while (input != "quit") {
    m68k_execute(100000);
    std::cin >> input;
    LOG(INFO) << "$ " << input;
  }
  return absl::OkStatus();
}