#include "emu/window_manager.h"

#include "emu/graphics/font/basic_font.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/quickdraw.h"
#include "emu/memory/memory_helpers.h"
#include "emu/memory/memory_map.h"

using ::cyder::graphics::BitmapImage;
using ::cyder::graphics::TempClipRect;
using ::cyder::memory::MemoryManager;

namespace cyder {
namespace {

constexpr uint8_t kWhitePattern[8] = {0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0x00, 0x00};

constexpr uint8_t kGreyPattern[8] = {0xAA, 0x55, 0xAA, 0x55,
                                     0xAA, 0x55, 0xAA, 0x55};

constexpr uint8_t kBlackPattern[8] = {0xFF, 0xFF, 0xFF, 0xFF,
                                      0xFF, 0xFF, 0xFF, 0xFF};

constexpr uint8_t kTitleBarPattern[8] = {0xFF, 0x00, 0xFF, 0x00,
                                         0xFF, 0x00, 0xFF, 0x00};

constexpr uint16_t kFrameTitleHeight = 16u;

// Return a clip region which represents the entire screen minus the menu bar
// FIXME: Grab this from the WinMgr's GrafPort::clip_region (once we have one)?
inline Rect CalculateDesktopRegion(const BitmapImage& screen) {
  return NewRect(0, 20, screen.width(), screen.height() - 20);
}

}  // namespace

WindowManager::WindowManager(NativeBridge& native_bridge,
                             EventManager& event_manager,
                             BitmapImage& screen,
                             MemoryManager& memory)
    : native_bridge_(native_bridge),
      event_manager_(event_manager),
      screen_(screen),
      memory_(memory) {}

absl::StatusOr<Ptr> WindowManager::NewWindow(Ptr window_storage,
                                             const Rect& bounds_rect,
                                             std::string title,
                                             bool is_visible,
                                             bool has_close,
                                             int16_t window_definition_id,
                                             Ptr behind_window,
                                             uint32_t reference_constant) {
  // If NULL is passed as |window_storage|, allocate space for the record.
  if (window_storage == 0) {
    window_storage = memory_.Allocate(WindowRecord::fixed_size);
  }

  auto port_frame = NormalizeRect(bounds_rect);

  // Returns a handle to a newly created Region defined by `rect`:
  auto create_rect_region =
      [&](Rect rect, const std::string& name) -> absl::StatusOr<Handle> {
    Region region;
    region.region_size = Rect::fixed_size;
    region.bounding_box = rect;

    return TRY(memory_.NewHandleFor<Region>(region, name));
  };

  auto create_title_handle = [&]() -> absl::StatusOr<Handle> {
    auto handle = memory_.AllocateHandle(title.size() + 1, "WindowTitle");
    auto memory = memory_.GetRegionForHandle(handle);
    RETURN_IF_ERROR(WriteType<absl::string_view>(title, memory, /*offset=*/0));
    return handle;
  };

  auto globals = TRY(port::GetQDGlobals());

  // FIXME: Fill in the rest of the WindowRecord (including GrafPort!)
  GrafPort port;
  // The |portBits.bounds| links the local and global coordinate systems
  // by offseting the screen bounds so that |portRect| appears at (0, 0).
  // For instance, if the window is meant to be drawn at (60, 60) global
  // then the origin of |portBits.bounds| is (-60, -60) local and
  // |portRect| has an origin of (0, 0) in local coordinates.
  //
  // See "Imaging with QuickDraw" Figure 2-4 for more details
  port.port_bits.bounds = OffsetRect(globals.screen_bits.bounds,
                                     -bounds_rect.left, -bounds_rect.top);
  port.port_rect = port_frame;
  // FIXME: This assumes the entire window is visible at creation
  port.visible_region = TRY(create_rect_region(port_frame, "VisibleRegion"));
  InitGrafPort(port);

  WindowRecord record;
  record.port = std::move(port);
  record.window_kind = 8 /*userKind*/;
  record.is_visible = is_visible;
  record.has_close = has_close;
  record.reference_constant = reference_constant;
  record.content_region = TRY(create_rect_region(bounds_rect, "ContentRegion"));
  record.update_region = TRY(create_rect_region(bounds_rect, "UpdateRegion"));
  record.title_handle = TRY(create_title_handle());
  record.structure_region =
      TRY(create_rect_region(bounds_rect, "StructRegion"));
  SetStructRegionAndDrawFrame(screen_, record, memory_);

  RESTRICT_FIELD_ACCESS(
      WindowRecord, window_storage,
      WindowRecordFields::port + GrafPortFields::visible_region,
      // TODO: Figure out a more elegant way to allow access to Rects
      WindowRecordFields::port + GrafPortFields::port_rect,
      WindowRecordFields::port + GrafPortFields::port_rect + 2,
      WindowRecordFields::port + GrafPortFields::port_rect + 4,
      WindowRecordFields::port + GrafPortFields::port_rect + 6,
      // TODO: Why is the `picture_handle` being accessed by "Window" demo?
      WindowRecordFields::picture_handle);

  RETURN_IF_ERROR(
      WriteType<WindowRecord>(record, memory::kSystemMemory, window_storage));

  window_list_.push_front(window_storage);
  return window_storage;
}

void WindowManager::DisposeWindow(Ptr window_ptr) {
  auto window_record =
      MUST(ReadType<WindowRecord>(memory::kSystemMemory, window_ptr));

  RepaintDesktopOverWindow(window_record);
  window_list_.remove(window_ptr);
  InvalidateWindows();
}

void WindowManager::NativeDragWindow(Ptr window_ptr, int x, int y) {
  auto window = MUST(ReadType<WindowRecord>(memory::kSystemMemory, window_ptr));
  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(window.structure_region));

  // Be careful with lambda captures as it will be called outside function scope
  DragGrayRegion(struct_region, {x, y}, [this, window_ptr](const Point& end) {
    auto status =
        WithType<WindowRecord>(window_ptr, [&](WindowRecord& window_record) {
          RepaintDesktopOverWindow(window_record);

          window_record.port.port_bits.bounds.left -= end.x;
          window_record.port.port_bits.bounds.top -= end.y;
          SetStructRegionAndDrawFrame(screen_, window_record, memory_);
          MoveToFront(window_ptr);
          InvalidateWindows();
          return absl::OkStatus();
        });
    CHECK(status.ok()) << "Failed WithType<WindowRecord>: "
                       << std::move(status).message();
  });
}

void WindowManager::DragGrayRegion(
    const Region& region,
    const Point& start,
    std::function<void(const Point&)> on_drag_end) {
  outline_rect_ = region.bounding_box;
  target_offset_ = {outline_rect_.left - start.x, outline_rect_.top - start.y};
  start_pt_ = start;

  on_drag_end_ = std::move(on_drag_end);
  native_bridge_.StartNativeMouseControl(this);
}

WindowManager::RegionType WindowManager::GetWindowAt(const Point& mouse,
                                                     Ptr& target_window) const {
  for (auto current_window : window_list_) {
    auto window_record =
        MUST(ReadType<WindowRecord>(memory::kSystemMemory, current_window));

    auto title_rect = GetRegionRect(window_record.structure_region);
    title_rect.bottom = title_rect.top + kFrameTitleHeight;

    if (PointInRect(mouse, title_rect)) {
      target_window = current_window;
      return RegionType::Drag;
    }

    auto content_rect = GetRegionRect(window_record.content_region);
    if (PointInRect(mouse, content_rect)) {
      target_window = current_window;
      return RegionType::Content;
    }
  }
  return RegionType::None;
}

void WindowManager::OnMouseMove(int x, int y) {
  TempClipRect _(screen_, CalculateDesktopRegion(screen_));

  int16_t outline_rect_width = RectWidth(outline_rect_);
  int16_t outline_rect_height = RectHeight(outline_rect_);

  if (!saved_bitmap_) {
    saved_bitmap_ =
        absl::make_unique<BitmapImage>(outline_rect_width, outline_rect_height);
  } else {
    screen_.CopyBitmap(*saved_bitmap_, NormalizeRect(outline_rect_),
                       outline_rect_);
  }

  outline_rect_ = NewRect(x + target_offset_.x, y + target_offset_.y,
                          outline_rect_width, outline_rect_height);

  saved_bitmap_->CopyBitmap(screen_, outline_rect_,
                            NormalizeRect(outline_rect_));
  screen_.FrameRect(outline_rect_, kBlackPattern);
}

void WindowManager::OnMouseUp(int x, int y) {
  TempClipRect _(screen_, CalculateDesktopRegion(screen_));

  // If window drag outline was drawn, restore the bitmap before anything else
  if (saved_bitmap_) {
    screen_.CopyBitmap(*saved_bitmap_, NormalizeRect(outline_rect_),
                       outline_rect_);
  }
  saved_bitmap_.reset();

  if (on_drag_end_) {
    std::move(on_drag_end_)({x - start_pt_.x, y - start_pt_.y});
  }
}

void WindowManager::MoveToFront(Ptr window_ptr) {
  CHECK(!window_list_.empty()) << "Window to move should be in list...";
  if (window_ptr == window_list_.front()) {
    return;
  }

  auto current_iter =
      std::find(window_list_.begin(), window_list_.end(), window_ptr);
  CHECK(current_iter != window_list_.cend())
      << "Window to move should be in list...";

  window_list_.splice(window_list_.begin(), window_list_, current_iter,
                      std::next(current_iter));
}

Ptr WindowManager::GetFrontWindow() const {
  if (window_list_.empty())
    return 0;

  return window_list_.front();
}

void WindowManager::InvalidateWindows() const {
  // Invalidate windows in reverse order per the painter's algorithm
  for (auto it = window_list_.rbegin(); it != window_list_.rend(); ++it) {
    event_manager_.QueueWindowUpdate(*it);
  }
}

void WindowManager::RepaintDesktopOverWindow(const WindowRecord& window) {
  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(window.structure_region));
  {
    // Clip to the part of the |struct_region| _within_ the desktop rect
    auto clip_rect = IntersectRect(CalculateDesktopRegion(screen_),
                                   struct_region.bounding_box);
    // Filling the full screen and clipping to the region ensures that the
    // pattern aligns with its surroundings (relative to the top-left corner
    // of the screen as opposed to |struct_region|).
    TempClipRect _(screen_, clip_rect);
    screen_.FillRect(NewRect(0, 0, screen_.width(), screen_.height()),
                     kGreyPattern);
  }
}

Rect WindowManager::GetRegionRect(Handle handle) const {
  auto region = MUST(memory_.ReadTypeFromHandle<Region>(handle));
  CHECK_EQ(region.size(), 10u) << "Requires rectangular Region";
  return region.bounding_box;
}

void SetStructRegionAndDrawFrame(BitmapImage& screen,
                                 WindowRecord& window,
                                 const MemoryManager& memory) {
  TempClipRect _(screen, CalculateDesktopRegion(screen));

  constexpr uint16_t kFrameWidth = 1u;

  Rect global_port_rect =
      OffsetRect(window.port.port_rect, -window.port.port_bits.bounds.left,
                 -window.port.port_bits.bounds.top);

  Rect frame_rect;
  frame_rect.left = global_port_rect.left - kFrameWidth;
  frame_rect.right = global_port_rect.right + kFrameWidth;
  frame_rect.top = global_port_rect.top - kFrameWidth;
  frame_rect.bottom = global_port_rect.bottom + kFrameWidth;
  screen.FillRect(frame_rect, kWhitePattern);
  screen.FrameRect(frame_rect, kBlackPattern);

  Rect title_bar_rect = NewRect(
      global_port_rect.left - kFrameWidth, frame_rect.top - kFrameTitleHeight,
      RectWidth(global_port_rect) + kFrameWidth * 2, kFrameTitleHeight);

  screen.FillRect(title_bar_rect, kTitleBarPattern);
  screen.FrameRect(title_bar_rect, kBlackPattern);

  Region region;
  region.bounding_box = UnionRect(frame_rect, title_bar_rect);
  CHECK(memory.WriteTypeToHandle<Region>(region, window.structure_region).ok());

  auto title =
      MUST(memory.ReadTypeFromHandle<std::string>(window.title_handle));

  constexpr uint16_t kTitlePaddingWidth = 4u;
  uint16_t title_width = title.size() * 8;

  Rect title_rect = NewRect((RectWidth(title_bar_rect) - title_width) / 2 +
                                title_bar_rect.left - kTitlePaddingWidth,
                            title_bar_rect.top + kFrameWidth,
                            title_width + kTitlePaddingWidth * 2,
                            kFrameTitleHeight - kFrameWidth * 2);

  screen.FillRect(title_rect, kWhitePattern);
  DrawString(screen, title, title_rect.left + kTitlePaddingWidth,
             title_bar_rect.top + ((kFrameTitleHeight - 8) / 2));
}

}  // namespace cyder
