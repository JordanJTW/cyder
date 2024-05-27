// Copyright (c) 2022, Jordan Werthman
// SPDX-License-Identifier: BSD-2-Clause

#include "emu/segment_loader.h"

#include "core/logging.h"
#include "core/memory_region.h"
#include "emu/memory/memory_map.h"
#include "emu/rsrc/resource.h"
#include "gen/global_names.h"

namespace cyder {
namespace memory {
extern core::MemoryRegion kSystemMemory;
}  // namespace memory

namespace {

constexpr bool kEnableLogging = false;

#define LOG_SEG(level) LOG_IF(level, kEnableLogging)

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
  RETURN_IF_ERROR(
      memory::kSystemMemory.Write<uint32_t>(a5_world_offset + 16, handle));
  // This info should also be accessible through the AppParmHandle global
  RETURN_IF_ERROR(
      memory::kSystemMemory.Write<uint32_t>(GlobalVars::AppParmHandle, handle));
  return absl::OkStatus();
}

}  // namespace

using ::cyder::rsrc::Resource;
using ::cyder::rsrc::ResourceGroup;

// static
absl::StatusOr<SegmentLoader> SegmentLoader::Create(
    memory::MemoryManager& memory_manager,
    ResourceManager& resource_manager) {
  const Resource* const segment_zero = resource_manager.GetSegmentZero();
  if (segment_zero == nullptr) {
    return absl::FailedPreconditionError("Missing 'CODE' Segment 0");
  }

  const core::MemoryRegion& table_data = segment_zero->GetData();

  auto header = TRY(ReadType<SegmentTableHeader>(table_data, /*offset=*/0));

  CHECK_EQ(header.table_size,
           table_data.size() - SegmentTableHeader::fixed_size);
  CHECK_EQ(header.table_offset, 32u)
      << "Jump table offset should always be 32 bytes";

  RETURN_IF_ERROR(memory::SetA5WorldBounds(header.above_a5, header.below_a5));

  // Write all unloaded entries verbatim to system memory:
  RETURN_IF_ERROR(memory::kSystemMemory.WriteRaw(
      table_data.raw_ptr() + SegmentTableHeader::fixed_size,
      memory::GetA5WorldPosition() + header.table_offset, header.table_size));

  RETURN_IF_ERROR(WriteAppParams(memory_manager, memory::GetA5WorldPosition()));

  return SegmentLoader(memory_manager, resource_manager, std::move(header));
}

absl::StatusOr<Ptr> SegmentLoader::Load(uint16_t segment_id) {
  const Handle segment_handle =
      resource_manager_.GetResource('CODE', segment_id);

  const auto resource_data = memory_manager_.GetRegionForHandle(segment_handle);

  const bool is_far_model = (0xFFFF == TRY(resource_data.Read<uint16_t>(0)));
  // TODO: Add support for far model headers
  CHECK(!is_far_model) << "Far model jump-table is not yet supported";

  Ptr segment_header_size = is_far_model ? 0x28 : 0x04;

  uint16_t offset_in_table = TRY(resource_data.Read<uint16_t>(0));
  uint16_t table_entry_count = TRY(resource_data.Read<uint16_t>(2));

  LOG_SEG(INFO) << "Load Segment " << segment_id
                << " count: " << table_entry_count;

  uint32_t segment_table_offset = memory::GetA5WorldPosition() +
                                  table_header_.table_offset + offset_in_table;

  Ptr absolute_address = 0;
  for (int i = table_entry_count; i > 0; --i) {
    uint32_t offset = segment_table_offset + (i - 1) * 8;
    uint16_t routine_offset = TRY(memory::kSystemMemory.Read<uint16_t>(offset));

    absolute_address =
        resource_data.base_offset() + segment_header_size + routine_offset;

    LOG_SEG(INFO) << "Update entry #" << i << " for Segment " << segment_id
                  << " relative offset: " << std::hex << routine_offset
                  << " to absolute: " << absolute_address;

    SegmentTableEntry entry;
    entry.segment_id = segment_id;
    entry.jmp_instr = 0x4EF9;
    entry.address = absolute_address;

    RETURN_IF_ERROR(
        WriteType<SegmentTableEntry>(entry, memory::kSystemMemory, offset));
  }
  return absolute_address;
}

SegmentLoader::SegmentLoader(memory::MemoryManager& memory_manager,
                             ResourceManager& resource_manager,
                             SegmentTableHeader table_header)
    : memory_manager_(memory_manager),
      resource_manager_(resource_manager),
      table_header_(std::move(table_header)) {}

}  // namespace cyder