#include "absl/status/status.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "resource_file.h"
#include "third_party/musashi/src/m68k.h"

constexpr size_t total_size = 512 * 1024;
constexpr size_t heap_start = 32 * 1024;
constexpr size_t a5_position = total_size - 32 * 1024;
constexpr size_t stack_start = a5_position - 32 * 1024;

uint8_t s_memory_data[total_size];
core::MemoryRegion s_memory(&s_memory_data, total_size);

void CheckReadAccess(unsigned int address) {
  if (address <= stack_start && address >= heap_start) {
    return;
  }
  if (address < heap_start) {
    LOG(WARNING) << std::hex << "Reading global: " << address;
    return;
  }
  if (address < a5_position && address > stack_start) {
    LOG(WARNING) << std::hex << "Reading A5 world: " << (address - a5_position);
    return;
  }
  LOG(WARNING) << std::hex << "Reading: " << address;
}

void CheckWriteAccess(unsigned int address, unsigned int value) {
  CHECK(address < heap_start || address > heap_start + 32 * 1024)
      << "Heap should not be written";
  if (address < heap_start) {
    LOG(WARNING) << std::hex << "Writing global: " << address << " = " << value;
    return;
  }
  if (address < a5_position && address > stack_start) {
    LOG(WARNING) << std::hex << "Writing A5 world: " << (address - a5_position)
                 << " = " << value;
    return;
  }
}

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
  CheckReadAccess(address);
  return MUST(s_memory.Copy<uint8_t>(address));
}
unsigned int m68k_read_memory_16(unsigned int address) {
  CheckReadAccess(address);
  return be16toh(MUST(s_memory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_memory_32(unsigned int address) {
  CheckReadAccess(address);
  return be32toh(MUST(s_memory.Copy<uint32_t>(address)));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK(s_memory.Write<uint8_t>(address, value).ok());
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK(s_memory.Write<uint16_t>(address, htobe16(value)).ok());
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK(s_memory.Write<uint32_t>(address, htobe32(value)).ok()) << " unable to write to " << std::hex << address;
}

void cpu_instr_callback(unsigned int pc) {
  char buffer[255];
  size_t increment = m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
  LOG(INFO) << std::hex << pc << ": " << buffer;
  m68k_end_timeslice();
}

absl::StatusOr<size_t> Init(const rsrcloader::ResourceFile& resource_file) {
  rsrcloader::ResourceGroup* const code = resource_file.FindGroupByType('CODE');
  if (code == nullptr) {
    return absl::NotFoundError("Could not find any 'CODE' resource");
  }

  const auto& resources = code->GetResources();
  size_t heap_offset = 0;
  for (const auto& resource : code->GetResources()) {
    if (resource->GetId() == 0) {
      continue;
    }

    const auto& data = resource->GetData();
    const auto& segment =
        (0xFFFF == TRY(data.Copy<uint16_t>(0)) ? TRY(data.Create(0x28))
                                               : TRY(data.Create(0x04)));

    LOG(INFO) << "Resource " << resource->GetId() << " at [" << std::hex
              << (heap_start + heap_offset) << ", "
              << (heap_start + heap_offset + segment.size()) << "]";
    for (int i = 0; i < segment.size(); ++i) {
      RETURN_IF_ERROR(s_memory.Write<uint8_t>(heap_start + heap_offset + i,
                                              TRY(segment.Copy<uint8_t>(i))));
    }
    heap_offset += segment.size();
  }

  const auto* segment0 = code->FindById(0);

  // The jump table is described at:
  // http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/runtimehtml/RTArch-118.html#MARKER-9-35
  struct InMemoryTableHeader {
    uint32_t above_a5;
    uint32_t below_a5;
    uint32_t table_size;
    uint32_t table_offset;
  };

  const auto& data = segment0->GetData();
  InMemoryTableHeader header = TRY(data.Copy<InMemoryTableHeader>(0));

  printf("Above A5: 0x%x\n", be32toh(header.above_a5));
  printf("Below A5: 0x%x\n", be32toh(header.below_a5));
  header.table_size = be32toh(header.table_size);
  printf("Jump-Table Size: %d\n", header.table_size);
  printf("Jump-Table Offset: %d\n", be32toh(header.table_offset));

  CHECK_EQ(header.table_size, data.size() - sizeof(InMemoryTableHeader));

  RETURN_IF_ERROR(s_memory.Write(data.raw_ptr() + sizeof(InMemoryTableHeader),
                                 a5_position + 32, header.table_size));

  // Each entry in the jump table is 8-bytes long (an offset to the subroutine
  // within a segment followed by instructions to _LoadSeg)
  return be16toh(TRY(data.Copy<uint16_t>(sizeof(InMemoryTableHeader)))) +
         heap_start;
}

int illegal_instr_callback(int opcode) {
  LOG(WARNING) << "Illegal instruction: " << opcode;
  return 0;
}

absl::Status Main(const core::Args& args) {
  LOG(INFO) << std::hex << "Low: [0x0, 0x" << heap_start - 1 << "]"
            << " Heap: [0x" << heap_start << ", ...]"
            << " Stack: [..., 0x" << stack_start << "]"
            << " A5: 0x" << a5_position;

  auto file =
      TRY(rsrcloader::ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));
  size_t pc = TRY(Init(*file));
  LOG(INFO) << "Initialize PC: " << std::hex << pc;

  m68k_init();
  m68k_set_instr_hook_callback(cpu_instr_callback);
  m68k_set_illg_instr_callback(illegal_instr_callback);
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);

  m68k_set_reg(M68K_REG_A5, a5_position);
  m68k_set_reg(M68K_REG_SP, stack_start);
  m68k_set_reg(M68K_REG_PC, pc);

  std::string input;
  while (input != "quit") {
    m68k_execute(100000);
    // std::cin >> input;
    // LOG(INFO) << "$ " << input;
  }
  return absl::OkStatus();
}