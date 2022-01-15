#include "absl/status/status.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/disasm_trap.h"
#include "memory_map.h"
#include "resource_file.h"
#include "stack_helpers.h"
#include "third_party/musashi/src/m68k.h"

constexpr bool disassemble_log = false;

constexpr size_t global_app_name = 0x910;

bool single_step = false;

struct ResourceData {
  uint32_t base_addr;
  uint16_t table_offset;
  uint16_t table_count;
};

extern core::MemoryRegion kSystemMemory;

std::vector<ResourceData> s_rsrc_data;

absl::Status HandleALineTrap(uint16_t trap_op, int& return_addr_offset) {
  LOG(INFO) << "A-Line Exception: 0x" << std::hex << trap_op;
  // single_step = true;

  switch (trap_op) {
    case 0xA9F0: {
      uint16_t load_segment = TRY(Pop<uint16_t>(M68K_REG_USP));
      LOG(INFO) << "_LoadSeg(" << load_segment << ")";

      uint32_t update_info_addr =
          kA5Position + 32 + s_rsrc_data[load_segment].table_offset;
      for (int i = 0; i < s_rsrc_data[load_segment].table_count; ++i) {
        uint32_t offset = update_info_addr + i * 8;
        uint16_t routine_offset =
            be16toh(TRY(kSystemMemory.Copy<uint16_t>(offset)));

        LOG(INFO) << "Update entry #" << i << " for segment " << load_segment
                  << " relative offset: " << std::hex << routine_offset
                  << " to absolute: "
                  << s_rsrc_data[load_segment].base_addr + routine_offset;

        RETURN_IF_ERROR(
            kSystemMemory.Write<uint16_t>(offset, htobe16(load_segment)));
        RETURN_IF_ERROR(
            kSystemMemory.Write<uint16_t>(offset + 2, htobe16(0x4EF9)));
        RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
            offset + 4,
            htobe32(s_rsrc_data[load_segment].base_addr + routine_offset)));
        return_addr_offset = -6;
        return absl::OkStatus();
      }
    }
    case 0xa9c8: {
      uint16_t duration = TRY(Pop<uint16_t>(M68K_REG_USP));
      LOG(INFO) << "_SysBeep(" << duration << ")";
      return absl::OkStatus();
    }
    case 0xa9f4:
      LOG(INFO) << "_ExitToShell()";
      exit(0);
      return absl::OkStatus();
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Reached unimplemented trap: '", GetTrapName(trap_op), "'"));
  }
}

absl::StatusOr<uint32_t> HandleException(unsigned int address) {
  CHECK_LT(address, 0x100) << "Address 0x" << std::hex << address
                           << " is outside of the IVT";

  switch (address) {
    // 1010 Instruction Trap Handler
    case 0x28: {
      uint16_t status = TRY(Pop<uint16_t>(M68K_REG_SP));
      uint32_t rts_addr = TRY(Pop<uint32_t>(M68K_REG_SP)) + 2;

      uint16_t trap_op = be16toh(
          TRY(kSystemMemory.Copy<uint16_t>(m68k_get_reg(NULL, M68K_REG_PPC))));

      int return_addr_offset = 0;
      RETURN_IF_ERROR(HandleALineTrap(trap_op, return_addr_offset));
      rts_addr += return_addr_offset;

      RETURN_IF_ERROR(Push<uint32_t>(rts_addr, M68K_REG_SP));
      RETURN_IF_ERROR(Push<uint16_t>(status, M68K_REG_SP));
      return kExceptionReturnAddr;
    }
    default:
      return absl::UnimplementedError(
          absl::StrCat("Exception occured with no handler: ", address));
  }
}

unsigned int m68k_read_disassembler_8(unsigned int address) {
  return MUST(kSystemMemory.Copy<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return be16toh(MUST(kSystemMemory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return be32toh(MUST(kSystemMemory.Copy<uint32_t>(address)));
}

unsigned int m68k_read_memory_8(unsigned int address) {
  CheckReadAccess(address);
  return MUST(kSystemMemory.Copy<uint8_t>(address));
}
unsigned int m68k_read_memory_16(unsigned int address) {
  CheckReadAccess(address);
  return be16toh(MUST(kSystemMemory.Copy<uint16_t>(address)));
}
unsigned int m68k_read_memory_32(unsigned int address) {
  CheckReadAccess(address);

  if (address < kInterruptVectorTableEnd) {
    uint32_t value = MUST(HandleException(address));
    return value;
  }

  return be32toh(MUST(kSystemMemory.Copy<uint32_t>(address)));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK_STATUS(kSystemMemory.Write<uint8_t>(address, value))
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK_STATUS(kSystemMemory.Write<uint16_t>(address, htobe16(value)))
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK_STATUS(kSystemMemory.Write<uint32_t>(address, htobe32(value)))
      << " unable to write " << std::hex << value << " to " << address;
}

void cpu_instr_callback(unsigned int pc) {
  char buffer[255];
  size_t increment = m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
  LOG_IF(INFO, disassemble_log) << std::hex << pc << ": " << buffer;
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
    metadata.base_addr = kHeapStart + heap_offset;
    // Only works for near model currently...
    metadata.table_offset = be16toh(TRY(data.Copy<uint16_t>(0)));
    metadata.table_count = be16toh(TRY(data.Copy<uint16_t>(2)));
    s_rsrc_data.push_back(metadata);

    LOG(INFO) << "Loaded segment #" << resource->GetId() << " at [0x"
              << std::hex << (kHeapStart + heap_offset) << ", 0x"
              << (kHeapStart + heap_offset + segment.size()) << "]";
    for (int i = 0; i < segment.size(); ++i) {
      RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(
          kHeapStart + heap_offset + i, TRY(segment.Copy<uint8_t>(i))));
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

  header.above_a5 = be32toh(header.above_a5);
  header.below_a5 = be32toh(header.below_a5);
  header.table_size = be32toh(header.table_size);
  header.table_offset = be32toh(header.table_offset);
  CHECK_EQ(header.table_offset, 32u)
      << "Jump table offset should always be 32 bytes";

  SetA5WorldBounds(header.above_a5, header.below_a5);

  CHECK_EQ(header.table_size, data.size() - sizeof(InMemoryTableHeader));

  RETURN_IF_ERROR(
      kSystemMemory.Write(data.raw_ptr() + sizeof(InMemoryTableHeader),
                          kA5Position + 32, header.table_size));

  // Each entry in the jump table is 8-bytes long (an offset to the subroutine
  // within a segment followed by instructions to _LoadSeg)
  return be16toh(TRY(data.Copy<uint16_t>(sizeof(InMemoryTableHeader)))) +
         kHeapStart;
}

absl::Status Main(const core::Args& args) {
  auto file =
      TRY(rsrcloader::ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));
  size_t pc = TRY(Init(*file));
  LOG(INFO) << "Initialize PC: " << std::hex << pc;
  LOG(INFO) << "Memory Map: " << MemoryMapToStr();

  m68k_init();
  m68k_set_instr_hook_callback(cpu_instr_callback);
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);

  m68k_set_reg(M68K_REG_PC, pc);
  m68k_set_reg(M68K_REG_A5, kA5Position);
  m68k_set_reg(M68K_REG_USP, kUserStackStart);
  m68k_set_reg(M68K_REG_ISP, kInterruptStackStart);

  // Sets the size of the name to 0 so it is not read:
  RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(global_app_name, 0));
  RETURN_IF_ERROR(
      kSystemMemory.Write<uint16_t>(kExceptionReturnAddr, htobe16(0x4E73)));

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