#include "emu/window_manager.h"

#include "emu/graphics/font/basic_font.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/quickdraw.h"
#include "emu/memory/memory_helpers.h"
#include "emu/memory/memory_map.h"

using ::cyder::graphics::BitmapImage;
using ::cyder::graphics::TempClipRect;
using ::cyder::memory::kSystemMemory;
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

constexpr uint16_t kFrameTitleHeight = 17u;
constexpr uint16_t kFrameWidth = 1u;

// Return a clip region which represents the entire screen minus the menu bar
// FIXME: Grab this from the WinMgr's GrafPort::clip_region (once we have one)?
inline Rect CalculateDesktopRegion(const BitmapImage& screen) {
  return NewRect(0, 20, screen.width(), screen.height() - 20);
}

// Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-191.html
enum WindowType {
  kDocument = 0,       // standard document  window, no zoom box
  kDialog = 1,         // alert box or modal  dialog box
  kPlainDialog = 2,    // plain box
  kAltDialog = 3,      // plain box with shadow
  kNoGrowDoc = 4,      // movable window,  no size box or zoom box
  kMovableDialog = 5,  // movable modal dialog box
  kZoomDoc = 8,        // standard document window
  kZoomNoGrow = 12,    // zoomable, nonresizable  window
};

bool HasTitleBar(const WindowRecord& window_record) {
  uint8_t variation_type = window_record.window_definition_proc;

  switch (variation_type) {
    case WindowType::kDocument:
    case WindowType::kNoGrowDoc:
    case WindowType::kMovableDialog:
    case WindowType::kZoomDoc:
    case WindowType::kZoomNoGrow:
      return true;
    case WindowType::kAltDialog:
    case WindowType::kPlainDialog:
      return false;
    default:
      NOTREACHED() << "Unsupported window variation: " << variation_type;
      return false;
  }
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
    region.region_size = Region::fixed_size;
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
  record.title_handle = TRY(create_title_handle());
  record.title_width = title.size() * 8; // Assumes 8x8 fixed-width font

  auto create_empty_region = [&](std::string name) -> absl::StatusOr<Handle> {
    return TRY(memory_.NewHandleFor<Region>(Region{}, std::move(name)));
  };

  record.content_region = TRY(create_empty_region("ContentRegion"));
  record.structure_region = TRY(create_empty_region("StructRegion"));
  record.update_region = TRY(create_empty_region("UpdateRegion"));

  // The resource ID of the window definition function is in the upper
  // 12 bits of the definition ID ('WDEF' ID 0 is the default function provided
  // by the OS). The optional variation code is placed in the lower 4 bits.
  CHECK_EQ(window_definition_id & 0xFFF0, 0) << "Only 'WDEF' ID 0 is supported";

  // This is obviously not a Handle and as such should only be accessed by us!
  record.window_definition_proc = window_definition_id & 0x000F;
  UpdateWindowRegions(record, memory_);

  RESTRICT_FIELD_ACCESS(
      WindowRecord, window_storage,
      WindowRecordFields::port + GrafPortFields::visible_region,
      WindowRecordFields::port + GrafPortFields::port_rect,
      WindowRecordFields::window_kind, WindowRecordFields::structure_region);

  RETURN_IF_ERROR(
      WriteType<WindowRecord>(record, memory::kSystemMemory, window_storage));

  window_list_.push_front(window_storage);
  SelectWindow(window_storage);
  return window_storage;
}

void WindowManager::DisposeWindow(Ptr window_ptr) {
  auto window_record =
      MUST(ReadType<WindowRecord>(memory::kSystemMemory, window_ptr));

  RepaintDesktopOverWindow(window_record);
  window_list_.remove(window_ptr);
  InvalidateWindows();
}

void WindowManager::DragWindow(Ptr window_ptr, const Point& start) {
  auto window = MUST(ReadType<WindowRecord>(memory::kSystemMemory, window_ptr));
  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(window.structure_region));

  // Be careful with lambda captures as it will be called outside function scope
  DragGrayRegion(struct_region, start, [this, window_ptr](const Point& delta) {
    MoveWindow(window_ptr, MoveType::Relative, delta, /*bring_to_front=*/true);
  });
}

// Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-246.html
void WindowManager::MoveWindow(Ptr window_ptr,
                               MoveType move_type,
                               const Point& location,
                               bool bring_to_front) {
  auto window_record = MUST(ReadType<WindowRecord>(kSystemMemory, window_ptr));

  // 1. Clear the current window location with the desktop pattern
  RepaintDesktopOverWindow(window_record);

  // 2. Update the window bounds relative to the global origin
  switch (move_type) {
    case MoveType::Absolute:
      window_record.port.port_bits.bounds = MoveRect(
          window_record.port.port_bits.bounds, -location.x, -location.y);
      break;
    case MoveType::Relative:
      window_record.port.port_bits.bounds = OffsetRect(
          window_record.port.port_bits.bounds, -location.x, -location.y);
      break;
    default:
      NOTREACHED() << "Unknown WindowManager::MoveType";
  }
  UpdateWindowRegions(window_record, memory_);

  auto status =
      WriteType<WindowRecord>(window_record, kSystemMemory, window_ptr);
  CHECK(status.ok()) << "Failed to WriteType<WindowRecord>: "
                     << std::move(status).message();

  // 3. If the value of the front parameter is TRUE and the window is not
  //    active, MoveWindow makes it active by calling SelectWindow.
  // 4. Ensure that the windows are invalidated and redrawn
  if (bring_to_front && !window_record.hilited) {
    SelectWindow(window_ptr);  // Handles invalidating windows
  } else {
    InvalidateWindows();
  }
}

// Link: http://0.0.0.0:8000/docs/mac/Toolbox/Toolbox-234.html
void WindowManager::SelectWindow(Ptr target_ptr) {
  bool is_already_active =
      MUST(kSystemMemory.Read<Boolean>(
          target_ptr + WindowRecordFields::hilited.offset)) == 0xFF /*true*/;

  // If the specified window is already active, SelectWindow has no effect.
  if (is_already_active) {
    return;
  }

  // 1. Update windows so only |target_ptr| is active (hilited).
  for (auto window_ptr : window_list_) {
    auto status = kSystemMemory.Write<Boolean>(
        window_ptr + WindowRecordFields::hilited.offset,
        target_ptr == window_ptr ? 0xFF /*true*/ : 0x00 /*false*/);
    CHECK(status.ok()) << "Setting WindowRecord::hilited failed: "
                       << std::move(status).message();
  }
  // 2. Bring the specified window to the front
  MoveToFront(target_ptr);
  // 3. Generate the activate events to deactivate the previously active
  //    window and activate the specified window
  // FIXME: Handle deacitvating the previously active window?
  event_manager_.QueueWindowActivate(target_ptr);
  // 4. Ensure that the windows are invalidated and redrawn
  InvalidateWindows();
}

void WindowManager::DragGrayRegion(
    const Region& region,
    const Point& start,
    std::function<void(const Point&)> on_drag_end) {
  outline_rect_ = region.bounding_box;
  target_offset_.x = outline_rect_.left - start.x;
  target_offset_.y = outline_rect_.top - start.y;
  start_pt_ = start;

  on_drag_end_ = std::move(on_drag_end);
  native_bridge_.StartNativeMouseControl(this);
}

WindowManager::RegionType WindowManager::GetWindowAt(const Point& mouse,
                                                     Ptr& target_window) const {
  for (auto current_window : window_list_) {
    auto window_record =
        MUST(ReadType<WindowRecord>(memory::kSystemMemory, current_window));

    if (HasTitleBar(window_record)) {
      auto title_rect = GetRegionRect(window_record.structure_region);
      title_rect.bottom = title_rect.top + kFrameTitleHeight;

      if (PointInRect(mouse, title_rect)) {
        target_window = current_window;
        return RegionType::Drag;
      }
    }

    auto content_rect = GetRegionRect(window_record.content_region);
    if (PointInRect(mouse, content_rect)) {
      target_window = current_window;
      return RegionType::Content;
    }
  }
  return RegionType::None;
}

void WindowManager::OnMouseMove(const Point& mouse) {
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

  outline_rect_ =
      NewRect(mouse.x + target_offset_.x, mouse.y + target_offset_.y,
              outline_rect_width, outline_rect_height);

  saved_bitmap_->CopyBitmap(screen_, outline_rect_,
                            NormalizeRect(outline_rect_));
  screen_.FrameRect(outline_rect_, kBlackPattern);
}

void WindowManager::OnMouseUp(const Point& mouse) {
  TempClipRect _(screen_, CalculateDesktopRegion(screen_));

  // If window drag outline was drawn, restore the bitmap before anything else
  if (saved_bitmap_) {
    screen_.CopyBitmap(*saved_bitmap_, NormalizeRect(outline_rect_),
                       outline_rect_);
  }
  saved_bitmap_.reset();

  if (on_drag_end_) {
    std::move(on_drag_end_)(mouse - start_pt_);
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

void DrawWindowFrame(const WindowRecord& window, BitmapImage& screen) {
  TempClipRect _(screen, CalculateDesktopRegion(screen));

  auto struct_region = MUST(ReadHandleToType<Region>(window.structure_region));

  screen.FillRect(struct_region.bounding_box, kWhitePattern);
  screen.FrameRect(struct_region.bounding_box, kBlackPattern);

  if (!HasTitleBar(window)) {
    return;
  }

  auto title_bar_rect = struct_region.bounding_box;
  title_bar_rect.bottom = title_bar_rect.top + kFrameTitleHeight;

  screen.FrameRect(title_bar_rect, kBlackPattern);
  if (window.hilited) {
    // Inset the pattern to better match the look of Classic Mac OS 6:
    // https://cdn.osxdaily.com/wp-content/uploads/2010/02/mac-evolution-system-6.jpg
    screen.FillRect(InsetRect(title_bar_rect, 2, 3), kTitleBarPattern);
  }

  constexpr uint16_t kTitlePaddingWidth = 4u;

  auto title_rect = InsetRect(
      title_bar_rect,
      (RectWidth(title_bar_rect) - window.title_width) / 2 - kTitlePaddingWidth,
      kFrameWidth);

  screen.FillRect(title_rect, kWhitePattern);
  DrawString(
      screen, MUST(ReadHandleToType<absl::string_view>(window.title_handle)),
      title_rect.left + kTitlePaddingWidth,
      title_rect.top + (RectHeight(title_bar_rect) - 8 /*8x8 fixed font*/) / 2);
}

void UpdateWindowRegions(WindowRecord& window, const MemoryManager& memory) {
  Rect global_port_rect =
      OffsetRect(window.port.port_rect, -window.port.port_bits.bounds.left,
                 -window.port.port_bits.bounds.top);

  auto write_region_to_handle = [&](Handle handle, const Rect& rect) {
    Region region;
    region.region_size = 10;
    region.bounding_box = rect;
    auto status = memory.WriteTypeToHandle<Region>(region, handle);
    CHECK(status.ok()) << "WriteTypeToHandle<Region> failed: "
                       << std::move(status).message();
  };

  write_region_to_handle(window.content_region, global_port_rect);

  Rect struct_rect = InsetRect(global_port_rect, -kFrameWidth, -kFrameWidth);
  if (HasTitleBar(window)) {
    struct_rect.top -= kFrameTitleHeight;
  }
  write_region_to_handle(window.structure_region, struct_rect);
}

}  // namespace cyder
