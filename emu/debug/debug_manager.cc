#include "emu/debug/debug_manager.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

#include "absl/base/no_destructor.h"
#include "core/logging.h"

namespace cyder {
namespace {

class DebugManagerImpl : public DebugManager {
 public:
  DebugManagerImpl() = default;
  ~DebugManagerImpl() override = default;

  // Delete copy constructor and assignment operator to prevent copying.
  DebugManagerImpl(const DebugManagerImpl&) = delete;
  DebugManagerImpl& operator=(const DebugManagerImpl&) = delete;
  // Delete move constructor and assignment operator to prevent moving.
  DebugManagerImpl(DebugManagerImpl&&) = delete;
  DebugManagerImpl& operator=(DebugManagerImpl&&) = delete;

  void TagMemory(size_t start, size_t end, const std::string& tag) override {
    for (const auto& span : memory_tags_) {
      if (span == MemorySpan{start, end, tag}) {
        // If the exact span already exists, do nothing.
        return;
      }
    }
    memory_tags_.push_back({start, end, tag});
    std::sort(memory_tags_.begin(), memory_tags_.end());
  }

  void RecordWrite(size_t start, size_t end) override {
    std::vector<MemorySpan> new_memory_tags;

    MemorySpan new_span{start, end, ""};
    for (auto it = memory_tags_.begin(); it != memory_tags_.end(); ++it) {
      const MemorySpan& span = *it;
      if (!span.tag.empty()) {
        new_memory_tags.push_back(span);
        continue;
      }

      if (span.end < new_span.start || span.start > new_span.end) {
        new_memory_tags.push_back(span);
        continue;  // No overlap
      }

      new_span.start = std::min(new_span.start, span.start);
      new_span.end = std::max(new_span.end, span.end);
      new_span.writes += span.writes + 1;  // Increment write count
    }

    new_memory_tags.push_back(new_span);

    memory_tags_ = std::move(new_memory_tags);
    std::sort(memory_tags_.begin(), memory_tags_.end());
  }

  std::vector<MemorySpan> GetMemoryTags() const override {
    return memory_tags_;
  }

  void PrintMemoryMap() const override {
    std::vector<MemorySpan> depth;
    for (const auto& tag : memory_tags_) {
      while (!depth.empty() && tag.start >= depth.back().end)
        depth.pop_back();

      size_t indent = depth.size() * 2;
      depth.push_back(tag);

      for (size_t i = 0; i < indent; ++i) {
        std::cout << " ";
      }

      std::cout << "[0x" << std::hex << tag.start << ", 0x" << tag.end
                << "), Tag: \"" << tag.tag << "\", Writes: " << std::dec
                << tag.writes << "\n";
    }
    std::cout << std::dec << "Tracking " << memory_tags_.size() << " spans";
  }

  void Clear() override { memory_tags_.clear(); }

 private:
  std::vector<MemorySpan> memory_tags_;
};

}  // namespace

DebugManager& DebugManager::Instance() {
  static absl::NoDestructor<DebugManagerImpl> instance;
  return *instance;
}

std::ostream& operator<<(std::ostream& os, const MemorySpan& span) {
  return os << "MemorySpan{" << span.start << ", " << span.end << ", \""
            << span.tag << "\"}";
}

}  // namespace cyder
