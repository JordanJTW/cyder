#include "emu/controls/control_manager.h"

#include <cstdint>

#include "absl/status/statusor.h"
#include "core/status_helpers.h"
#include "emu/controls/control_manager.tdef.h"
#include "emu/memory/memory_manager.h"
#include "emu/rsrc/resource_manager.h"
#include "emu/window_manager.tdef.h"
#include "gen/typegen/typegen_prelude.h"

namespace cyder {
namespace control {

absl::StatusOr<Handle> GetNewControl(uint16_t control_id, WindowPtr owner) {
  ControlResource rsrc = TRY(
      ResourceManager::the().GetResource<ControlResource>('CNTL', control_id));

  ControlRecord output;
  output.contrlMax = rsrc.maximum_setting;
  output.contrlMin = rsrc.minimum_setting;
  output.contrlRect = rsrc.rect;
  output.contrlOwner = owner;
  output.contrlTitle = rsrc.title;
  // TODO: Set the rest of the fields

  Handle handle = memory::MemoryManager::the().AllocateHandle(output.size(),
                                                              "ControlRecord");
  Ptr ptr = memory::MemoryManager::the().GetPtrForHandle(handle);
  RETURN_IF_ERROR(WriteType<ControlRecord>(output, memory::kSystemMemory, ptr));
  return handle;
}

}  // namespace control
}  // namespace cyder
