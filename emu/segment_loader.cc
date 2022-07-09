#include "segment_loader.h"

#include "core/endian_helpers.h"
#include "core/logging.h"
#include "memory_map.h"

using rsrcloader::ResourceFile;
using rsrcloader::ResourceGroup;

// static
absl::StatusOr<SegmentLoader> SegmentLoader::Create(
    ResourceFile& resource_file,
    MemoryManager& memory_manager) {
  ResourceGroup* const code_group = resource_file.FindGroupByType('CODE');
  if (code_group == nullptr) {
    return absl::NotFoundError("Missing 'CODE' resource");
  }

  const auto* segement_zero = code_group->FindById(0);
  if (segement_zero == nullptr) {
    return absl::NotFoundError("Missing Segment 0");
  }

  const core::MemoryRegion& table_data = segement_zero->GetData();

  InMemoryTableHeader header = TRY(table_data.Copy<InMemoryTableHeader>(0));
  header.above_a5 = be32toh(header.above_a5);
  header.below_a5 = be32toh(header.below_a5);
  header.table_size = be32toh(header.table_size);
  header.table_offset = be32toh(header.table_offset);

  CHECK_EQ(header.table_size, table_data.size() - sizeof(InMemoryTableHeader));
  CHECK_EQ(header.table_offset, 32u)
      << "Jump table offset should always be 32 bytes";

  SetA5WorldBounds(header.above_a5, header.below_a5);

  // Write all unloaded entries verbatim to system memory:
  RETURN_IF_ERROR(kSystemMemory.Write(
      table_data.raw_ptr() + sizeof(InMemoryTableHeader),
      GetA5WorldPosition() + header.table_offset, header.table_size));

  // Unloaded entries in the jump-table begin with the relative offset to the
  // subroutine so assuming Segment 1 will be the first thing loaded into the
  // heap it's absolute address can be calculated.
  uint32_t initial_program_counter =
      be16toh(TRY(table_data.Copy<uint16_t>(sizeof(InMemoryTableHeader)))) +
      kHeapStart + MemoryManager::kHeapHandleOffset;

  return SegmentLoader(memory_manager, std::move(header), *code_group,
                       initial_program_counter);
}

absl::Status SegmentLoader::Load(uint16_t segment_id) {
  const auto* segment_resource = code_resources_.FindById(segment_id);
  if (segment_resource == nullptr) {
    return absl::NotFoundError(absl::StrCat("Missing Segment ", segment_id));
  }

  const auto& resourece_data = segment_resource->GetData();
  const auto& segment = (0xFFFF == TRY(resourece_data.Copy<uint16_t>(0))
                             ? TRY(resourece_data.Create(0x28))
                             : TRY(resourece_data.Create(0x04)));

  // TODO: Add support for far model headers
  uint16_t offset_in_table = be16toh(TRY(resourece_data.Copy<uint16_t>(0)));
  uint16_t table_entry_count = be16toh(TRY(resourece_data.Copy<uint16_t>(2)));

  Handle handle = memory_manager_.Allocate(
      segment.size(), absl::StrCat("Segment(id:", segment_id, ")"));

  size_t load_addr = be32toh(TRY(kSystemMemory.Copy<uint32_t>(handle)));
  LOG(INFO) << "Load Segment " << segment_id << " at "
            << "[0x" << std::hex << load_addr << ", 0x"
            << (load_addr + segment.size()) << "] count: " << table_entry_count;

  // TODO: Add an allocator instead of just sequential offsets
  for (int i = 0; i < segment.size(); ++i) {
    RETURN_IF_ERROR(kSystemMemory.Write<uint8_t>(
        load_addr + i, TRY(segment.Copy<uint8_t>(i))));
  }

  uint32_t segment_table_offset =
      GetA5WorldPosition() + table_header_.table_offset + offset_in_table;
  for (int i = 0; i < table_entry_count; ++i) {
    uint32_t offset = segment_table_offset + i * 8;
    uint16_t routine_offset =
        be16toh(TRY(kSystemMemory.Copy<uint16_t>(offset)));

    LOG(INFO) << "Update entry #" << i << " for Segment " << segment_id
              << " relative offset: " << std::hex << routine_offset
              << " to absolute: " << load_addr + routine_offset;

    // Writes out a loaded entry as described in:
    // http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/runtimehtml/RTArch-118.html#MARKER-9-38
    //
    // Segment ID         (2 bytes)
    // 'JMP' instruction  (2 bytes)
    // Absolute address   (4 bytes)
    RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(offset, htobe16(segment_id)));
    RETURN_IF_ERROR(kSystemMemory.Write<uint16_t>(offset + 2, htobe16(0x4EF9)));
    RETURN_IF_ERROR(kSystemMemory.Write<uint32_t>(
        offset + 4, htobe32(load_addr + routine_offset)));
  }
  return absl::OkStatus();
}

SegmentLoader::SegmentLoader(MemoryManager& memory_manager,
                             InMemoryTableHeader table_header,
                             ResourceGroup& code_resources,
                             uint32_t initial_program_counter)
    : memory_manager_(memory_manager),
      table_header_(std::move(table_header)),
      code_resources_(code_resources),
      initial_program_counter_(initial_program_counter) {}
