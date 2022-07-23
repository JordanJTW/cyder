#include <iomanip>
#include <tuple>

#include "absl/status/status.h"
#include "emu/memory/memory_manager.h"
#include "emu/segment_loader.h"
#include "resource_file.h"

struct SDL_Renderer;

namespace cyder {
namespace trap {

// Handles dispatching from emulated A-Traps to native code
class TrapManager {
 public:
  TrapManager(memory::MemoryManager& memory_manager,
              ResourceManager& resource_manager,
              SegmentLoader& segment_loader,
              SDL_Renderer* renderer);

  absl::Status DispatchEmulatedSubroutine(uint32_t address);

  absl::Status PerformTrapEntry();
  absl::Status PerformTrapDispatch();
  absl::Status PerformTrapExit();

 private:
  absl::Status DispatchNativeSystemTrap(uint16_t trap);
  absl::Status DispatchNativeToolboxTrap(uint16_t trap);

  uint32_t GetTrapAddress(uint16_t trap);

  memory::MemoryManager& memory_manager_;
  ResourceManager& resource_manager_;
  SegmentLoader& segment_loader_;
  SDL_Renderer* renderer_;

  std::map<uint16_t, uint32_t> patch_trap_addresses_;
};

}  // namespace trap
}  // namespace cyder