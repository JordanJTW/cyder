#include "emu/window_manager.h"

#include "emu/graphics/font/basic_font.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/quickdraw.h"
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

  // Returns handle to a new Region which represents the local dimensions:
  auto create_port_region =
      [&](const std::string& name) -> absl::StatusOr<Handle> {
    Region region;
    region.region_size = Rect::fixed_size;
    region.bounding_box = port_frame;

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
  port.visible_region = TRY(create_port_region("VisibleRegion"));

  WindowRecord record;
  record.port = std::move(port);
  record.window_kind = 8 /*userKind*/;
  record.is_visible = is_visible;
  record.has_close = has_close;
  record.reference_constant = reference_constant;
  record.content_region = TRY(create_port_region("ContentRegion"));
  record.title_handle = TRY(create_title_handle());
  record.structure_region = TRY(create_port_region("StructRegion"));
  SetStructRegionAndDrawFrame(screen_, record, memory_);

  // RESTRICT_FIELD_ACCESS(
  //     WindowRecord, window_storage,
  //     WindowRecordFields::port + GrafPortFields::visible_region,
  //     WindowRecordFields::port + GrafPortFields::port_rect,
  //     WindowRecordFields::port + GrafPortFields::port_rect + 4,
  //     WindowRecordFields::port + GrafPortFields::text_font,
  //     WindowRecordFields::has_zoom, WindowRecordFields::window_kind);

  RETURN_IF_ERROR(
      WriteType<WindowRecord>(record, memory::kSystemMemory, window_storage));
  return window_storage;
}

void WindowManager::NativeDragWindow(Ptr window_ptr, int x, int y) {
  native_bridge_.StartNativeMouseControl(this);
  target_window_ptr_ = window_ptr;

  target_window_ =
      MUST(ReadType<WindowRecord>(memory::kSystemMemory, target_window_ptr_));

  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(target_window_.structure_region));
  target_offset_.x = struct_region.bounding_box.left - x;
  target_offset_.y = struct_region.bounding_box.top - y;

  outline_rect_ = struct_region.bounding_box;
}

bool WindowManager::CheckIsWindowDrag(Ptr window_ptr, int x, int y) const {
  auto window_record =
      MUST(ReadType<WindowRecord>(memory::kSystemMemory, window_ptr));

  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(window_record.structure_region));

  if (x < struct_region.bounding_box.left ||
      x > struct_region.bounding_box.right ||
      y < struct_region.bounding_box.top ||
      y > struct_region.bounding_box.top + kFrameTitleHeight) {
    return false;
  }
  return true;
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
  if (!target_window_ptr_)
    return;

  TempClipRect _(screen_, CalculateDesktopRegion(screen_));

  // If window drag outline was drawn, restore the bitmap before anything else
  if (saved_bitmap_) {
    screen_.CopyBitmap(*saved_bitmap_, NormalizeRect(outline_rect_),
                       outline_rect_);
  }

  auto struct_region =
      MUST(memory_.ReadTypeFromHandle<Region>(target_window_.structure_region));
  {
    // This ensures that the pattern aligns with its surroundings (based off
    // of the top-left corner of the screen as opposed to |struct_region|).
    TempClipRect _(screen_, struct_region.bounding_box);
    screen_.FillRect(NewRect(0, 0, screen_.width(), screen_.height()),
                     kGreyPattern);
  }

  // TODO: What causes the +1 to be needed? (Double-clicking the title bar
  //       shifts the window up and to the left otherwise...)
  target_window_.port.port_bits.bounds.left = -(x + target_offset_.x + 1);
  target_window_.port.port_bits.bounds.top =
      -(y + target_offset_.y + kFrameTitleHeight + 1);
  SetStructRegionAndDrawFrame(screen_, target_window_, memory_);

  CHECK(WriteType<WindowRecord>(target_window_, memory::kSystemMemory,
                                target_window_ptr_)
            .ok());

  event_manager_.QueueWindowUpdate(target_window_ptr_);
  target_window_ptr_ = 0;
  saved_bitmap_.reset();
}

void SetStructRegionAndDrawFrame(BitmapImage& screen,
                                 WindowRecord& window,
                                 const MemoryManager& memory) {
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
