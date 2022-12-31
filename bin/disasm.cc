// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "core/endian_helpers.h"
#include "core/memory_region.h"
#include "core/status_helpers.h"
#include "core/status_main.h"
#include "emu/segment_types.tdef.h"
#include "gen/trap_names.h"
#include "resource_file.h"
#include "third_party/musashi/src/m68k.h"

using namespace rsrcloader;

const core::MemoryRegion* codeSegment;

unsigned int m68k_read_disassembler_8(unsigned int address) {
  return MUST(codeSegment->Read<uint8_t>(address));
}
unsigned int m68k_read_disassembler_16(unsigned int address) {
  return MUST(codeSegment->Read<uint16_t>(address));
}
unsigned int m68k_read_disassembler_32(unsigned int address) {
  return MUST(codeSegment->Read<uint32_t>(address));
}

// TODO: Figure out where this is documented...
// From: https://github.com/ksherlock/mpw/blob/master/bin/disasm.cpp#L171-L197
size_t SkipDebugSection(uint16_t op, const core::MemoryRegion& remaining_data) {
  size_t offset = 0;

  // Read the first byte as the length of the debug string
  uint8_t debug_string_len = (op >> 8) - 0x80;
  offset += 1;

  offset += debug_string_len;
  // Ensure that offset is rounded up to the next even offset
  offset = (offset + 1) & ~0x01;

  // Treat the remainder as data until op: 0x4E56 [Link A6,#]
  while (offset < remaining_data.size()) {
    uint16_t op = MUST(remaining_data.Read<uint16_t>(offset));
    if (op == 0x4e56)
      break;
    offset += 2;
  }
  return offset;
}

bool IsToolboxTrap(uint16_t op) {
  return (op & 0xF000) == 0xA000;
}

// TODO: Figure out where this is documented...
// From: github.com/ksherlock/mpw/blob/master/bin/disasm.cpp#L167-L170
bool IsDebugSection(uint16_t prevOp, uint16_t op) {
  constexpr uint16_t OP_RTS = 0x4E75;
  constexpr uint16_t OP_RTD = 0x4E74;

  return (prevOp == OP_RTS || prevOp == OP_RTD) && op > 0x8000;
}

absl::Status ParseJumpTable(const core::MemoryRegion& data) {
 auto header = TRY(ReadType<SegmentTableHeader>(data, /*offset=*/0));

  printf("Above A5: 0x%x\n", header.above_a5);
  printf("Below A5: 0x%x\n", header.below_a5);
  printf("Jump-Table Size: %d\n", header.table_size);
  printf("Jump-Table Offset: %d\n", header.table_offset);

  CHECK_EQ(header.table_size, data.size() - SegmentTableHeader::fixed_size);

  // Each entry in the jump table is 8-bytes long (an offset to the subroutine
  // within a segment followed by instructions to _LoadSeg)
  for (size_t i = 0; i < header.table_size; i = i + 8) {
    size_t entry_offset = SegmentTableHeader::fixed_size + i;
    printf("Offset (relative to segment): %x\n",
           TRY(data.Read<uint16_t>(entry_offset)));
    CHECK_EQ(TRY(data.Read<uint16_t>(entry_offset + 6)),
             /*_SegLoad:*/ 0xA9F0)
        << "Expected to find _SegLoad op-code in jump entry";
  }
  return absl::OkStatus();
}

absl::Status ParseSegment(uint16_t id, const core::MemoryRegion& data) {
  codeSegment = &data;

  size_t countTraps = 0;
  uint16_t prevOp = 0;
  size_t pc = 0;

  while (pc < codeSegment->size()) {
    uint16_t op = TRY(codeSegment->Read<uint16_t>(pc));

    if (IsDebugSection(prevOp, op)) {
      size_t offset = SkipDebugSection(op, TRY(data.Create(pc)));
      printf("Skipped %lu bytes of debug symbols\n", offset);
      pc += offset;
      prevOp = 0;
      continue;
    }

    char buffer[255];
    size_t increment = m68k_disassemble(buffer, pc, M68K_CPU_TYPE_68000);

    std::string line = buffer;
    if (IsToolboxTrap(op)) {
      line = std::string(GetTrapName(op));
      countTraps++;
    }

    printf("%lx: %s\n", (pc), line.c_str());
    pc += increment;
    prevOp = op;
  }

  printf("Traps called %lu times...\n", countTraps);
  return absl::OkStatus();
}

absl::Status ParseCode(const Resource& resource) {
  const auto& data = resource.GetData();
  printf("\nParsing Segment %d which is %lu bytes\n", resource.GetId(),
         data.size());

  if (resource.GetId() == 0) {
    return ParseJumpTable(data);
  }

  // A code resource can contain either a near (4-byte header) or far (28-byte
  // header) segment. The far model header can be identified by checking that
  // the first two bytes are 0xFFFF as documented here:
  //   mirror.informatimago.com/next/developer.apple.com/documentation/mac/runtimehtml/RTArch-128.html
  if (0xFFFF == TRY(data.Read<uint16_t>(0))) {
    return ParseSegment(resource.GetId(), TRY(data.Create(0x28)));
  }

  return ParseSegment(resource.GetId(), TRY(data.Create(0x04)));
}

absl::Status Main(const core::Args& args) {
  auto file = TRY(ResourceFile::Load(TRY(args.GetArg(1, "FILENAME"))));

  const ResourceGroup* const code = file->FindGroupByType('CODE');
  if (code == nullptr) {
    return absl::NotFoundError("Could not find any 'CODE' resource");
  }

  for (const auto& resource : code->GetResources()) {
    RETURN_IF_ERROR(ParseCode(resource));
  }
  return absl::OkStatus();
}
