#include "absl/status/status.h"
#include "core/endian.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "memory_manager.h"
#include "memory_map.h"
#include "resource_file.h"
#include "segment_loader.h"
#include "stack_helpers.h"
#include "third_party/musashi/src/m68k.h"
#include "trap_helpers.h"
#include "trap_names.h"

constexpr bool disassemble_log = false;
constexpr bool memory_write_log = false;

constexpr size_t kGlobalAppNameAddr = 0x910;

bool single_step = false;

extern core::MemoryRegion kSystemMemory;

typedef std::function<void(uint32_t)> on_exception_callback_t;

on_exception_callback_t on_exception_callback = nullptr;

// RAII class to capture the return address on the stack while processing an
// exception, modify it, then push it back on to the stack.
class ExceptionReturn {
 public:
  ExceptionReturn()
      : status_(MUST(Pop<uint32_t>(M68K_REG_SP))),
        return_address_(MUST(Pop<uint16_t>(M68K_REG_SP))) {}

  ~ExceptionReturn() {
    CHECK(Push<uint32_t>(return_address_ + return_offset_, M68K_REG_SP).ok());
    CHECK(Push<uint16_t>(status_, M68K_REG_SP).ok());
  }

  void SetReturnOffset(int16_t return_offset) {
    return_offset_ = return_offset;
  }

 private:
  // By default skip past the instruction that triggered the exception when
  // returning (the instruction should be 2-bytes wide).
  int16_t return_offset_{2};

  uint16_t status_;
  uint32_t return_address_;
};

absl::Status HandleALineTrap(SegmentLoader& segment_loader,
                             rsrcloader::ResourceFile& current_rsrc,
                             MemoryManager& memory_manager,
                             uint16_t trap,
                             ExceptionReturn& stack_return) {
  LOG(INFO) << "A-Line Exception " << (trap::IsToolbox(trap) ? "Toolbox" : "OS")
            << "::" << GetTrapName(trap) << " (0x" << std::hex << trap
            << ") Index: " << std::dec << trap::ExtractIndex(trap)
            << " Flags: " << trap::ExtractFlags(trap);

  switch (trap) {
    case Trap::LoadSeg: {
      uint16_t load_segment = TRY(Pop<uint16_t>(M68K_REG_USP));
      LOG(INFO) << "TRAP LoadSeg(" << load_segment << ")";
      RETURN_IF_ERROR(segment_loader.Load(load_segment));
      // The segment loader modifies the six byte entry for this segment in the
      // table so return to the begining of the entry (4 bytes behind the 2 byte
      // trap being executed):
      stack_return.SetReturnOffset(-4);
      return absl::OkStatus();
    }
    case Trap::SysBeep: {
      uint16_t duration = TRY(Pop<uint16_t>(M68K_REG_USP));
      LOG(INFO) << "TRAP SysBeep(" << duration << ")";
      return absl::OkStatus();
    }
    case Trap::ExitToShell:
      LOG(INFO) << "TRAP ExitToShell()";
      exit(0);
      return absl::OkStatus();
    default:
      return absl::UnimplementedError(absl::StrCat(
          "Reached unimplemented trap: '", GetTrapName(trap), "'"));
  }
}

absl::Status HandleException(SegmentLoader& segment_loader,
                             rsrcloader::ResourceFile& current_rsrc,
                             MemoryManager& memory_manager,
                             unsigned int address) {
  CHECK_LT(address, 0x100) << "Address 0x" << std::hex << address
                           << " is outside of the IVT";

  switch (address) {
    // 1010 Instruction Trap Handler
    case 0x28: {
      uint16_t trap_op = be16toh(
          TRY(kSystemMemory.Copy<uint16_t>(m68k_get_reg(NULL, M68K_REG_PPC))));

      ExceptionReturn exception_return;
      RETURN_IF_ERROR(HandleALineTrap(segment_loader, current_rsrc,
                                      memory_manager, trap_op,
                                      exception_return));
      return absl::OkStatus();
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
    if (on_exception_callback) {
      on_exception_callback(address);
    }
    return kExceptionReturnAddr;
  }

  return be32toh(MUST(kSystemMemory.Copy<uint32_t>(address)));
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(kSystemMemory.Write<uint8_t>(address, value).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_16(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(kSystemMemory.Write<uint16_t>(address, htobe16(value)).ok())
      << " unable to write " << std::hex << value << " to " << address;
}
void m68k_write_memory_32(unsigned int address, unsigned int value) {
  CheckWriteAccess(address, value);
  LOG_IF(INFO, memory_write_log)
      << std::hex << __func__ << "(" << address << ": " << value << ")";
  CHECK(kSystemMemory.Write<uint32_t>(address, htobe32(value)).ok())
      << " unable to write " << std::hex << value << " to " << address;
}

MemoryManager* memory_manager_ptr;

void cpu_instr_callback(unsigned int pc) {
  CHECK(pc != 0) << "Reset";
  if (disassemble_log) {
    char buffer[255];
    size_t increment = m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);
    Handle handle = memory_manager_ptr->GetHandleThatContains(pc);
    std::string tag = (handle == 0) ? "" : memory_manager_ptr->GetTag(handle);
    LOG(INFO) << std::hex << pc << " (" << tag << "): " << buffer;
  }
  CHECK(m68k_get_reg(NULL, M68K_REG_USP) <= kUserStackStart);
  CHECK(m68k_get_reg(NULL, M68K_REG_ISP) <= kInterruptStackStart);
  m68k_end_timeslice();
}

absl::Status Main(const core::Args& args) {
  auto file =
      TRY(rsrcloader::ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));

  MemoryManager memory_manager;
  memory_manager_ptr = &memory_manager;

  auto segment_loader = TRY(SegmentLoader::Create(*file, memory_manager));
  RETURN_IF_ERROR(segment_loader.Load(1));

  size_t pc = segment_loader.entry_point();
  LOG(INFO) << "Initialize PC: " << std::hex << pc;
  LOG(INFO) << "Memory Map: " << MemoryMapToStr();

  on_exception_callback = [&](uint32_t address) {
    auto status =
        HandleException(segment_loader, *file, memory_manager, address);
    CHECK(status.ok()) << std::move(status).message();
  };

  m68k_init();
  m68k_set_instr_hook_callback(cpu_instr_callback);
  m68k_set_cpu_type(M68K_CPU_TYPE_68000);

  m68k_set_reg(M68K_REG_PC, pc);
  m68k_set_reg(M68K_REG_A5, kA5Position);
  m68k_set_reg(M68K_REG_USP, kUserStackStart);
  m68k_set_reg(M68K_REG_ISP, kInterruptStackStart);

  // Sets the size of the name to 0 so it is not read:
  // TODO: Store the application name here as a Pascal string
  RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(kGlobalAppNameAddr, 0));
  // Stores the 'RTE' op-code at the exception return address to
  // jump back from the exception handler back to user code:
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
