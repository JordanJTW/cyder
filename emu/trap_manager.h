#include <iomanip>
#include <tuple>

#include "absl/status/status.h"
#include "memory_manager.h"
#include "resource_file.h"
#include "segment_loader.h"
#include "stack_helpers.h"

struct SDL_Renderer;

class TrapManager {
 public:
  TrapManager(MemoryManager& memory_manager,
              rsrcloader::ResourceFile& resource_file,
              SegmentLoader& segment_loader,
              SDL_Renderer* renderer);

  absl::Status HandleALineTrap(uint16_t trap);

 private:
  MemoryManager& memory_manager_;
  rsrcloader::ResourceFile& resource_file_;
  SegmentLoader& segment_loader_;
  SDL_Renderer* renderer_;
};
