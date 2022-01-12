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

bool single_step = false;

struct ResourceData {
  uint32_t base_addr;
  uint16_t table_offset;
  uint16_t table_count;
};

std::vector<ResourceData> s_rsrc_data;

void CheckReadAccess(unsigned int address) {
  if (address <= stack_start && address >= heap_start) {
    return;
  }
  if (address < heap_start) {
    LOG(WARNING) << std::hex << "Reading global: " << address;
    return;
  }
  if (address < a5_position && address > stack_start) {
    LOG(WARNING) << std::hex << "Reading A5 world: -0x"
                 << (a5_position - address);
    return;
  }
  if (address < total_size - 8 && address > m68k_get_reg(NULL, M68K_REG_ISP)) {
    LOG(WARNING) << "Reading IStack: 0x"
                 << (address - m68k_get_reg(NULL, M68K_REG_ISP));
    return;
  }
  LOG(WARNING) << std::hex << "Reading: " << address << " Heap: " << heap_start
               << " Stack: " << stack_start << " A5: " << a5_position;
}

void CheckWriteAccess(unsigned int address, unsigned int value) {
  CHECK(address < heap_start || address > heap_start + 32 * 1024)
      << "Heap should not be written";
  if (address < heap_start) {
    LOG(WARNING) << std::hex << "Writing global: " << address << " = " << value;
    return;
  }
  if (address < a5_position && address > stack_start) {
    LOG(WARNING) << std::hex << "Writing A5 world: -0x"
                 << (a5_position - address) << " = " << value;
    return;
  }
  if (address < total_size - 8 &&
      address > m68k_get_reg(NULL, M68K_REG_ISP) - 4) {
    LOG(WARNING) << "Writing to IStack: 0x" << std::hex
                 << (address - m68k_get_reg(NULL, M68K_REG_ISP)) << " = "
                 << value;
    return;
  }
  if (address < stack_start && address > heap_start) {
    LOG(WARNING) << "Writing to stack: " << std::hex << (stack_start - address)
                 << " = " << value << " (" << address << ")";
    return;
  }
  LOG(WARNING) << std::hex << "Writing: " << address << " Heap: " << heap_start
               << " Stack: " << stack_start << " A5: " << a5_position;
}

template <typename T>
absl::StatusOr<T> BasePop(m68k_register_t stack_ptr_reg) {
  uint32_t stack_ptr = m68k_get_reg(NULL, stack_ptr_reg);
  T value = TRY(s_memory.Copy<T>(stack_ptr));
  m68k_set_reg(stack_ptr_reg, stack_ptr + sizeof(T));
  return value;
}

template <typename T>
absl::StatusOr<T> Pop(m68k_register_t stack_ptr_reg) {
  return BasePop<T>(stack_ptr_reg);
}

template <>
absl::StatusOr<uint16_t> Pop(m68k_register_t stack_ptr_reg) {
  return be16toh(TRY(BasePop<uint16_t>(stack_ptr_reg)));
}

template <>
absl::StatusOr<uint32_t> Pop(m68k_register_t stack_ptr_reg) {
  return be32toh(TRY(BasePop<uint32_t>(stack_ptr_reg)));
}

template <typename T>
absl::Status BasePush(T value, m68k_register_t stack_ptr_reg) {
  uint32_t new_stack_ptr = m68k_get_reg(NULL, stack_ptr_reg) - sizeof(T);
  RETURN_IF_ERROR(s_memory.Write<T>(new_stack_ptr, value));
  m68k_set_reg(stack_ptr_reg, new_stack_ptr);
  return absl::OkStatus();
}

template <typename T>
absl::Status Push(T value, m68k_register_t stack_ptr_reg) {
  return BasePush<T>(value, stack_ptr_reg);
}

template <>
absl::Status Push(uint16_t value, m68k_register_t stack_ptr_reg) {
  return BasePush<uint16_t>(htobe16(value), stack_ptr_reg);
}

template <>
absl::Status Push(uint32_t value, m68k_register_t stack_ptr_reg) {
  return BasePush<uint32_t>(htobe32(value), stack_ptr_reg);
}

uint32_t HandleALineTrap(uint16_t trap_op, uint32_t ret_addr) {
  LOG(INFO) << "A-Line Exception: 0x" << std::hex << trap_op;
  // single_step = true;

  switch (trap_op) {
    case 0xA9F0: {
      uint16_t load_segment = MUST(Pop<uint16_t>(M68K_REG_USP));
      LOG(INFO) << "_LoadSeg(" << load_segment << ") = " << std::hex
                << s_rsrc_data[load_segment].base_addr << ", "
                << s_rsrc_data[load_segment].table_offset << ", "
                << s_rsrc_data[load_segment].table_count;

      uint32_t update_info_addr =
          a5_position + 32 + s_rsrc_data[load_segment].table_offset;
      for (int i = 0; i < s_rsrc_data[load_segment].table_count; ++i) {
        uint32_t offset = update_info_addr + i * 8;
        uint16_t routine_offset =
            be16toh(MUST(s_memory.Copy<uint16_t>(offset)));

        LOG(INFO) << "Update entry #" << i << " for segment " << load_segment
                  << " relative offset: " << std::hex << routine_offset
                  << " to absolute: "
                  << s_rsrc_data[load_segment].base_addr + routine_offset;

        CHECK(s_memory.Write<uint16_t>(offset, htobe16(load_segment)).ok());
        CHECK(s_memory.Write<uint16_t>(offset + 2, htobe16(0x4EF9)).ok());
        CHECK(s_memory
                  .Write<uint32_t>(offset + 4,
                                   htobe32(s_rsrc_data[load_segment].base_addr +
                                           routine_offset))
                  .ok());
        return ret_addr - 4;
      }
    }
    case 0xa9c8:
      LOG(INFO) << "BEEP!";
      return ret_addr + 2;
    case 0xa9f4:
      LOG(INFO) << "Goodbye...";
      exit(0);
      return ret_addr;
    default:
      LOG(FATAL) << "Unimplemented trap: 0x" << std::hex << trap_op;
      return ret_addr;
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
  if (address == 0x28) {
    uint16_t trap_op = be16toh(
        MUST(s_memory.Copy<uint16_t>(m68k_get_reg(NULL, M68K_REG_PPC))));
    uint16_t status = MUST(Pop<uint16_t>(M68K_REG_SP));
    uint32_t rts_addr = MUST(Pop<uint32_t>(M68K_REG_SP));

    rts_addr = HandleALineTrap(trap_op, rts_addr);

    CHECK(Push<uint32_t>(rts_addr, M68K_REG_SP).ok());
    CHECK(Push<uint16_t>(status, M68K_REG_SP).ok());
    return total_size - 2;
  }
  return be32toh(MUST(s_memory.Copy<uint32_t>(address)));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK(s_memory.Write<uint8_t>(address, value).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK(s_memory.Write<uint16_t>(address, htobe16(value)).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK(s_memory.Write<uint32_t>(address, htobe32(value)).ok())
      << " unable to write " << std::hex << value << " to " << address;
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
      s_rsrc_data.push_back({});
      continue;
    }

    const auto& data = resource->GetData();
    const auto& segment =
        (0xFFFF == TRY(data.Copy<uint16_t>(0)) ? TRY(data.Create(0x28))
                                               : TRY(data.Create(0x04)));

    ResourceData metadata;
    metadata.base_addr = heap_start + heap_offset;
    // Only works for near model currently...
    metadata.table_offset = be16toh(TRY(data.Copy<uint16_t>(0)));
    metadata.table_count = be16toh(TRY(data.Copy<uint16_t>(2)));
    s_rsrc_data.push_back(metadata);

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
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);

  m68k_set_reg(M68K_REG_A5, a5_position);
  m68k_set_reg(M68K_REG_SP, stack_start);
  m68k_set_reg(M68K_REG_PC, pc);
  m68k_set_reg(M68K_REG_ISP, total_size - 8);

  LOG(INFO) << "MSP: " << m68k_get_reg(NULL, M68K_REG_MSP)
            << " SP: " << m68k_get_reg(NULL, M68K_REG_SP)
            << " USP: " << m68k_get_reg(NULL, M68K_REG_USP)
            << " ISP: " << m68k_get_reg(NULL, M68K_REG_ISP);

  RETURN_IF_ERROR(s_memory.Write<uint16_t>(total_size - 2, htobe16(0x4E73)));

  std::string input;
  while (input != "quit") {
    m68k_execute(100000);
    if (single_step) {
      std::cin >> input;
      if (input.find("run") != std::string::npos) {
        single_step = false;
      }
    }
    // LOG(INFO) << "$ " << input;
  }
  return absl::OkStatus();
}