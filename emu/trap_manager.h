#include <iomanip>
#include <tuple>

#include "absl/status/status.h"
#include "memory_manager.h"
#include "resource_file.h"
#include "segment_loader.h"
#include "stack_helpers.h"

struct SDL_Renderer;

// Handles dispatching from emulated A-Traps to native code
class TrapManager {
 public:
  TrapManager(MemoryManager& memory_manager,
              rsrcloader::ResourceManager& resource_manager,
              SegmentLoader& segment_loader,
              SDL_Renderer* renderer);

  absl::Status DispatchEmulatedSubroutine(uint32_t address);

  absl::Status PerformTrapEntry();
  absl::Status PerformTrapDispatch();
  absl::Status PerformTrapExit();

 private:
  absl::Status DispatchTrap(uint16_t trap);

  MemoryManager& memory_manager_;
  rsrcloader::ResourceManager& resource_manager_;
  SegmentLoader& segment_loader_;
  SDL_Renderer* renderer_;
};
