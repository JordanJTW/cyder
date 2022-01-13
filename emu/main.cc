#include <set>

#include "absl/status/status.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/disasm_trap.h"
#include "resource_file.h"
#include "third_party/musashi/src/m68k.h"

constexpr bool verbose_logging = false;
constexpr bool disassemble_log = false;

constexpr size_t total_size = 512 * 1024;
constexpr size_t heap_start = 32 * 1024;
constexpr size_t a5_position = total_size - 32 * 1024;
constexpr size_t stack_size = 4 * 1024;
constexpr size_t user_stack_start = a5_position - 32 * 1024;
constexpr size_t intr_stack_start = total_size - 8;
constexpr size_t user_stack_end = user_stack_start - stack_size;
constexpr size_t intr_stack_end = intr_stack_start - stack_size;
constexpr size_t heap_end = user_stack_start - stack_size;
constexpr size_t intr_return = total_size - 2;

constexpr size_t global_app_name = 0x910;

size_t below_a5;
size_t above_a5;

uint8_t s_memory_data[total_size];
core::MemoryRegion s_memory(&s_memory_data, total_size);

bool single_step = false;

struct ResourceData {
  uint32_t base_addr;
  uint16_t table_offset;
  uint16_t table_count;
};

std::vector<ResourceData> s_rsrc_data;

std::set<uint32_t> app_global_writes;

void CheckReadAccess(unsigned int address) {
  auto within_region = [&](size_t lower, size_t upper) {
    return address >= lower && address < upper;
  };
  auto within_stack = [&](size_t start, size_t end) {
    return address > end && address <= start;
  };

  // Interrupt Vector Table (handled below)
  if (within_region(0, 0x100)) {
    return;
  }

  // Global Variables / OS Reserved
  if (within_region(0x100, heap_start)) {
    // AppName (STR[31]):
    if (within_region(0x910, 0x932)) {
      return;
    }

    LOG(WARNING) << std::hex << "Read un-initialized system global [0x100, 0x"
                 << heap_start << "): 0x" << address;
    return;
  }

  // Application Heap
  if (within_region(heap_start, heap_end)) {
    return;
  }

  // User Stack
  if (within_stack(user_stack_start, user_stack_end)) {
    LOG_IF(INFO, verbose_logging)
        << "Read User Stack: 0x" << std::hex << address << " (0x"
        << (user_stack_start - address) << ")";
    return;
  }

  if (address == a5_position) {
    LOG(WARNING) << "Read A5 (Pointer to QuickDraw): 0x" << std::hex << address;
    return;
  }
  if (within_region(a5_position - below_a5, a5_position)) {
    LOG_IF(INFO, verbose_logging) << "Read below A5: 0x" << std::hex << address
                                  << " (-0x" << (a5_position - address) << ")";
    for (uint32_t write : app_global_writes) {
      if (write == address)
        return;
    }
    LOG(WARNING) << "Read un-initialized below A5: 0x" << std::hex << address
                 << " (-0x" << (a5_position - address) << ")";
    return;
  }
  if (within_region(a5_position, a5_position + above_a5)) {
    if (address < a5_position + 32) {
      LOG(WARNING) << "Read unimplemented application parameters: 0x"
                   << std::hex << address << " (0x" << (address - a5_position)
                   << ")";
      return;
    }
    LOG_IF(INFO, verbose_logging) << "Read above A5: 0x" << std::hex << address
                                  << " (+0x" << (address - a5_position) << ")";
    return;
  }

  // Interrupt Stack
  if (within_stack(intr_stack_start, intr_stack_end)) {
    LOG_IF(INFO, verbose_logging)
        << "Read Interrupt Stack: 0x" << std::hex << address << " (0x"
        << (intr_stack_start - address) << ")";
    return;
  }

  // RTE
  if (address == intr_return) {
    return;
  }

  LOG(FATAL) << "Untracked read: 0x" << std::hex << address;
}

void CheckWriteAccess(unsigned int address, unsigned int value) {
  auto within_region = [&](size_t lower, size_t upper) {
    return address >= lower && address < upper;
  };
  auto within_stack = [&](size_t start, size_t end) {
    return address > end && address <= start;
  };

  // Interrupt Vector Table (handled below)
  CHECK(!within_region(0, 0x100))
      << "IVT is read-only: 0x" << std::hex << address;

  // Global Variables / OS Reserved
  if (within_region(0x100, heap_start)) {
    LOG(WARNING) << std::hex << "Write system global [0x100, 0x" << heap_start
                 << "): 0x" << address;
    return;
  }

  // Application Heap
  if (within_region(heap_start, heap_end)) {
    LOG(WARNING) << "Write to application heap: 0x" << std::hex << address;
    return;
  }

  // User Stack
  if (within_stack(user_stack_start, user_stack_end)) {
    LOG_IF(INFO, verbose_logging)
        << "Write User Stack: 0x" << std::hex << address << " (0x"
        << (user_stack_start - address) << ")";
    return;
  }

  if (address == a5_position) {
    LOG(WARNING) << "Write A5 (Pointer to QuickDraw): 0x" << std::hex
                 << address;
    return;
  }
  if (within_region(a5_position - below_a5, a5_position)) {
    LOG_IF(INFO, verbose_logging)
        << "Write below A5 (app globals): 0x" << std::hex << address << " (-0x"
        << (a5_position - address) << ")";
    app_global_writes.insert(address);
    return;
  }
  if (within_region(a5_position, a5_position + above_a5)) {
    if (address < a5_position + 32) {
      LOG(WARNING) << "Write unimplemented application parameters: 0x"
                   << std::hex << address << " (0x" << (address - a5_position)
                   << ")";
      return;
    }
    LOG(WARNING) << "Write above A5: 0x" << std::hex << address << " (+0x"
                 << (address - a5_position) << ")";
    return;
  }

  // Interrupt Stack
  if (within_stack(intr_stack_start, intr_stack_end)) {
    LOG_IF(INFO, verbose_logging)
        << "Write Interrupt Stack: 0x" << std::hex << address << " (0x"
        << (intr_stack_start - address) << ")";
    return;
  }

  // RTE
  CHECK(address != intr_return) << "Interrupt return instruction is read-only";

  LOG(FATAL) << "Untracked write: 0x" << std::hex << address;
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

absl::Status HandleALineTrap(uint16_t trap_op, int& return_addr_offset) {
  LOG(INFO) << "A-Line Exception: 0x" << std::hex << trap_op;
  // single_step = true;

  switch (trap_op) {
    case 0xA9F0: {
      uint16_t load_segment = TRY(Pop<uint16_t>(M68K_REG_USP));
      LOG(INFO) << "_LoadSeg(" << load_segment << ")";

      uint32_t update_info_addr =
          a5_position + 32 + s_rsrc_data[load_segment].table_offset;
      for (int i = 0; i < s_rsrc_data[load_segment].table_count; ++i) {
        uint32_t offset = update_info_addr + i * 8;
        uint16_t routine_offset = be16toh(TRY(s_memory.Copy<uint16_t>(offset)));

        LOG(INFO) << "Update entry #" << i << " for segment " << load_segment
                  << " relative offset: " << std::hex << routine_offset
                  << " to absolute: "
                  << s_rsrc_data[load_segment].base_addr + routine_offset;

        RETURN_IF_ERROR(
            s_memory.Write<uint16_t>(offset, htobe16(load_segment)));
        RETURN_IF_ERROR(s_memory.Write<uint16_t>(offset + 2, htobe16(0x4EF9)));
        RETURN_IF_ERROR(s_memory.Write<uint32_t>(
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
          TRY(s_memory.Copy<uint16_t>(m68k_get_reg(NULL, M68K_REG_PPC))));

      int return_addr_offset = 0;
      RETURN_IF_ERROR(HandleALineTrap(trap_op, return_addr_offset));
      rts_addr += return_addr_offset;

      RETURN_IF_ERROR(Push<uint32_t>(rts_addr, M68K_REG_SP));
      RETURN_IF_ERROR(Push<uint16_t>(status, M68K_REG_SP));
      return total_size - 2;
    }
    default:
      return absl::UnimplementedError(
          absl::StrCat("Exception occured with no handler: ", address));
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

  if (address < 0x100) {
    uint32_t value = MUST(HandleException(address));
    return value;
  }

  return be32toh(MUST(s_memory.Copy<uint32_t>(address)));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK_STATUS(s_memory.Write<uint8_t>(address, value))
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK_STATUS(s_memory.Write<uint16_t>(address, htobe16(value)))
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  CHECK_STATUS(s_memory.Write<uint32_t>(address, htobe32(value)))
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
    metadata.base_addr = heap_start + heap_offset;
    // Only works for near model currently...
    metadata.table_offset = be16toh(TRY(data.Copy<uint16_t>(0)));
    metadata.table_count = be16toh(TRY(data.Copy<uint16_t>(2)));
    s_rsrc_data.push_back(metadata);

    LOG(INFO) << "Loaded segment #" << resource->GetId() << " at [0x"
              << std::hex << (heap_start + heap_offset) << ", 0x"
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

  header.above_a5 = be32toh(header.above_a5);
  header.below_a5 = be32toh(header.below_a5);
  header.table_size = be32toh(header.table_size);
  header.table_offset = be32toh(header.table_offset);
  CHECK_EQ(header.table_offset, 32u)
      << "Jump table offset should always be 32 bytes";

  above_a5 = header.above_a5;
  below_a5 = header.below_a5;

  CHECK_EQ(header.table_size, data.size() - sizeof(InMemoryTableHeader));

  RETURN_IF_ERROR(s_memory.Write(data.raw_ptr() + sizeof(InMemoryTableHeader),
                                 a5_position + 32, header.table_size));

  // Each entry in the jump table is 8-bytes long (an offset to the subroutine
  // within a segment followed by instructions to _LoadSeg)
  return be16toh(TRY(data.Copy<uint16_t>(sizeof(InMemoryTableHeader)))) +
         heap_start;
}

absl::Status Main(const core::Args& args) {
  auto file =
      TRY(rsrcloader::ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));
  size_t pc = TRY(Init(*file));
  LOG(INFO) << "Initialize PC: " << std::hex << pc;

  LOG(INFO) << std::hex << "Heap: [0x" << heap_start << ", 0x" << heap_end
            << "]"
            << " User Stack: [0x" << user_stack_end << ", 0x" << user_stack_end
            << "]"
            << " Interrupt Stack: [0x" << intr_stack_end << ", 0x"
            << intr_stack_start << "]"
            << " A5: 0x" << a5_position << " (+" << above_a5 << ", -"
            << below_a5 << ")";

  m68k_init();
  m68k_set_instr_hook_callback(cpu_instr_callback);
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);

  m68k_set_reg(M68K_REG_A5, a5_position);
  m68k_set_reg(M68K_REG_SP, user_stack_start);
  m68k_set_reg(M68K_REG_PC, pc);
  m68k_set_reg(M68K_REG_ISP, intr_stack_start);

  // Sets the size of the name to 0 so it is not read:
  RETURN_IF_ERROR(s_memory.Write<uint8_t>(global_app_name, 0));
  RETURN_IF_ERROR(s_memory.Write<uint16_t>(intr_return, htobe16(0x4E73)));

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