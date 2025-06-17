#pragma once

#include <cstddef>
#include <deque>
#include <ostream>
#include <string>

#include "core/logging.h"

namespace cyder {

struct MemorySpan {
  size_t start;
  size_t end;
  std::string tag;
  size_t writes = 0;

  bool operator==(const MemorySpan& other) const {
    return start == other.start && end == other.end && tag == other.tag;
  }

  bool operator<(const MemorySpan& other) const {
    if (start != other.start)
      return start < other.start;
    if (end != other.end)
      return end > other.end;
    return tag < other.tag;
  }
};

class DebugManager {
 public:
  static DebugManager& Instance();

  virtual ~DebugManager() = default;

  virtual void RecordWrite(size_t start, size_t end) = 0;

  virtual void TagMemory(size_t address,
                         size_t size,
                         const std::string& tag) = 0;

  virtual std::vector<MemorySpan> GetMemoryTags() const = 0;

  virtual void PrintMemoryMap() const = 0;

  virtual void Clear() = 0;
};

std::ostream& operator<<(std::ostream& os, const MemorySpan& span);

}  // namespace cyder
