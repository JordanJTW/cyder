#include "emu/segment_loader.h"

#include "core/endian_helpers.h"
#include "core/logging.h"
#include "core/memory_region.h"
#include "emu/memory/memory_map.h"
#include "resource.h"

namespace cyder {
namespace memory {
extern core::MemoryRegion kSystemMemory;
}  // namespace memory

namespace {

// Link: https://macgui.com/news/article.php?t=523
absl::Status WriteAppParams(memory::MemoryManager& memory_manager,
                            size_t a5_world_offset) {
  // Standard Input (0 = Keyboard)
  RETURN_IF_ERROR(
      memory::kSystemMemory.Write<uint32_t>(a5_world_offset + 8, 0));
  // Standard Output (0 = Screen)
  RETURN_IF_ERROR(
      memory::kSystemMemory.Write<uint32_t>(a5_world_offset + 12, 0));

  // Writes a simple Finder Information structure with nothing to open.
  // More information can be found in Inside Macintosh Volume II (pg. 55-56).
  // FIXME: Allow passing a file to open when starting an application.
  auto handle = memory_manager.AllocateHandle(/*size=*/4, "FinderInfo");
  auto finder_info = memory_manager.GetRegionForHandle(handle);
  RETURN_IF_ERROR(finder_info.Write<uint16_t>(/*offset=*/0, 0 /*open*/));
  RETURN_IF_ERROR(finder_info.Write<uint16_t>(/*offset=*/2, 0 /*count*/));

  // Finder Information Handle
  RETURN_IF_ERROR(memory::kSystemMemory.Write<uint32_t>(
      a5_world_offset + 16, htobe<uint32_t>(handle)));
  return absl::OkStatus();
}

}  // namespace

using rsrcloader::Resource;
using rsrcloader::ResourceGroup;

// static
absl::StatusOr<SegmentLoader> SegmentLoader::Create(
    memory::MemoryManager& memory_manager,
    ResourceManager& resource_manager) {
  Resource* const segment_zero = resource_manager.GetSegmentZero();
  if (segment_zero == nullptr) {
    return absl::FailedPreconditionError("Missing 'CODE' Segment 0");
  }

  const core::MemoryRegion& table_data = segment_zero->GetData();

  InMemoryTableHeader header = TRY(table_data.Copy<InMemoryTableHeader>(0));
  header.above_a5 = be32toh(header.above_a5);
  header.below_a5 = be32toh(header.below_a5);
  header.table_size = be32toh(header.table_size);
  header.table_offset = be32toh(header.table_offset);

  CHECK_EQ(header.table_size, table_data.size() - sizeof(InMemoryTableHeader));
  CHECK_EQ(header.table_offset, 32u)
      << "Jump table offset should always be 32 bytes";

  RETURN_IF_ERROR(memory::SetA5WorldBounds(header.above_a5, header.below_a5));

  // Write all unloaded entries verbatim to system memory:
  RETURN_IF_ERROR(memory::kSystemMemory.Write(
      table_data.raw_ptr() + sizeof(InMemoryTableHeader),
      memory::GetA5WorldPosition() + header.table_offset, header.table_size));

  RETURN_IF_ERROR(WriteAppParams(memory_manager, memory::GetA5WorldPosition()));

  return SegmentLoader(memory_manager, resource_manager, std::move(header));
}

absl::StatusOr<Ptr> SegmentLoader::Load(uint16_t segment_id) {
  const Handle segment_handle =
      resource_manager_.GetResource('CODE', segment_id);

  const auto resource_data = memory_manager_.GetRegionForHandle(segment_handle);

  const bool is_far_model = (0xFFFF == TRY(resource_data.Copy<uint16_t>(0)));
  // TODO: Add support for far model headers
  CHECK(!is_far_model) << "Far model jump-table is not yet supported";

  Ptr segment_header_size = is_far_model ? 0x28 : 0x04;

  uint16_t offset_in_table = be16toh(TRY(resource_data.Copy<uint16_t>(0)));
  uint16_t table_entry_count = be16toh(TRY(resource_data.Copy<uint16_t>(2)));

  LOG(INFO) << "Load Segment " << segment_id << " count: " << table_entry_count;

  uint32_t segment_table_offset = memory::GetA5WorldPosition() +
                                  table_header_.table_offset + offset_in_table;

  Ptr absolute_address = 0;
  for (int i = table_entry_count; i > 0; --i) {
    uint32_t offset = segment_table_offset + (i - 1) * 8;
    uint16_t routine_offset =
        be16toh(TRY(memory::kSystemMemory.Copy<uint16_t>(offset)));

    absolute_address =
        resource_data.base_offset() + segment_header_size + routine_offset;

    LOG(INFO) << "Update entry #" << i << " for Segment " << segment_id
              << " relative offset: " << std::hex << routine_offset
              << " to absolute: " << absolute_address;

    // Writes out a loaded entry as described in:
    // http://mirror.informatimago.com/next/developer.apple.com/documentation/mac/runtimehtml/RTArch-118.html#MARKER-9-38
    //
    // Segment ID         (2 bytes)
    // 'JMP' instruction  (2 bytes)
    // Absolute address   (4 bytes)
    RETURN_IF_ERROR(
        memory::kSystemMemory.Write<uint16_t>(offset, htobe16(segment_id)));
    RETURN_IF_ERROR(
        memory::kSystemMemory.Write<uint16_t>(offset + 2, htobe16(0x4EF9)));
    RETURN_IF_ERROR(memory::kSystemMemory.Write<uint32_t>(
        offset + 4, htobe32(absolute_address)));
  }
  return absolute_address;
}

SegmentLoader::SegmentLoader(memory::MemoryManager& memory_manager,
                             ResourceManager& resource_manager,
                             InMemoryTableHeader table_header)
    : memory_manager_(memory_manager),
      resource_manager_(resource_manager),
      table_header_(std::move(table_header)) {}

}  // namespace cyder