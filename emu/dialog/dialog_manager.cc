#include "emu/dialog/dialog_manager.h"

#include "core/status_helpers.h"
#include "emu/font/font.h"
#include "emu/graphics/grafport_types.tdef.h"
#include "emu/graphics/graphics_helpers.h"
#include "emu/graphics/pict_v1.h"
#include "emu/graphics/quickdraw.h"
#include "emu/memory/memory_helpers.h"
#include "emu/memory/memory_manager.h"
#include "emu/memory/memory_map.h"
#include "emu/rsrc/resource_manager.h"
#include "emu/window_manager.h"
#include "third_party/abseil-cpp/absl/status/statusor.h"
#include "third_party/abseil-cpp/absl/strings/escaping.h"
#include "third_party/musashi/src/m68k.h"

extern bool single_step;

namespace cyder::dialog {
namespace {

using memory::MemoryManager;

// `dialogKind` constant from:
// https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-296.html#HEADING296-94
constexpr Integer kDialogKind = 2;

enum ItemType {
  btnCtrl = 4,    // standard button control
  chkCtrl = 5,    // standard checkbox control
  radCtrl = 6,    // standard radio button
  resCtrl = 7,    // control defined in a 'CNTL'
  helpItem = 1,   // help balloons
  statText = 8,   // static text
  editText = 16,  // editable text
  iconItem = 32,  // icon
  picItem = 64,   // QuickDraw picture
  userItem = 0,   // application-defined item
  itemDisable = 128,
};

std::string ItemType_Name(ItemType item_type) {
  switch (item_type) {
    case btnCtrl:
      return "Button";
    case chkCtrl:
      return "Checkbox";
    case radCtrl:
      return "Radio Button";
    case resCtrl:
      return "'CNTL' Control";
    case helpItem:
      return "Help";
    case statText:
      return "Static Text";
    case editText:
      return "Edit Text";
    case iconItem:
      return "Icon";
    case picItem:
      return "Picture";
    case userItem:
      return "Custom";
    default:
      return "Unknown";
  }
}

enum IterationControl { NEXT_ITERATION, STOP_ITERATION };

absl::Status IterateItems(
    const core::MemoryRegion& items_memory,
    std::function<absl::StatusOr<IterationControl>(Integer, size_t)> cb) {
  core::MemoryReader reader(items_memory);

  // Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-438.html
  Integer item_count = TRY(reader.Next<Integer>());

  for (Integer i = 0; i <= item_count; ++i) {
    if (TRY(cb(i + 1, reader.offset())) == STOP_ITERATION) {
      break;
    }

    // Skip past Reserved (4 bytes) + Display Rect (8 bytes)
    reader.SkipNext(12);

    // Lower 7 bits is the item type (upper bit is enable flag)
    uint8_t item_type = TRY(reader.Next<uint8_t>());
    item_type = item_type & 0x7f;

    switch (item_type) {
      case ItemType::btnCtrl:
      case ItemType::chkCtrl:
      case ItemType::radCtrl:
      case ItemType::statText:
      case ItemType::editText: {
        uint8_t length = TRY(reader.Next<uint8_t>());
        reader.SkipNext(length + (length % 2));
        continue;
      }

      case ItemType::resCtrl:
      case ItemType::iconItem:
      case ItemType::picItem:
        reader.SkipNext(3);
        continue;

      case ItemType::userItem:
        reader.SkipNext(1);
        continue;

      case ItemType::helpItem: {
        uint8_t size = TRY(reader.Next<uint8_t>());
        reader.SkipNext(size);
        continue;
      }
    }
  }
  return absl::OkStatus();
}

absl::Status DrawDialogWindow(WindowPtr window_ptr) {
  const DialogRecord dialog_record =
      TRY(ReadType<DialogRecord>(memory::kSystemMemory, window_ptr));

  CHECK_EQ(dialog_record.window_record.window_kind, kDialogKind)
      << "Passed WindowRecord must be a dialogKind";

  core::MemoryRegion item_memory =
      MemoryManager::the().GetRegionForHandle(dialog_record.items);

  graphics::BitmapImage screen = graphics::ThePortImage();
  return IterateItems(
      item_memory,
      [&](Integer item_no, size_t offset) -> absl::StatusOr<IterationControl> {
        core::MemoryReader reader(item_memory, offset);

        ItemHeader header = TRY(reader.NextType<ItemHeader>());
        auto item_type = static_cast<ItemType>(header.type_and_disabled & 0x7f);

        switch (item_type) {
          case ItemType::btnCtrl: {
            absl::string_view text = TRY(reader.NextString());

            GrafPort port = dialog_record.window_record.port;
            Rect global_box = port::LocalToGlobal(port, header.box);
            screen.FrameRect(global_box, port.pen_pattern.bytes);
            SystemFont().DrawString(screen, text, global_box.left,
                                    global_box.top);
            break;
          }
          case ItemType::statText: {
            absl::string_view text = TRY(reader.NextString());

            GrafPort port = dialog_record.window_record.port;
            Rect global_box = port::LocalToGlobal(port, header.box);
            SystemFont().DrawString(screen, text, global_box.left,
                                    global_box.top);
            break;
          }
          case ItemType::picItem: {
            reader.SkipNext(1);
            Integer resource_id = TRY(reader.Next<Integer>());
            Handle handle =
                ResourceManager::the().GetResource('PICT', resource_id);
            auto pict_data = MemoryManager::the().GetRegionForHandle(handle);

            auto pict_frame = TRY(graphics::GetPICTFrame(pict_data));

            size_t picture_size =
                PixelWidthToBytes(pict_frame.right) * pict_frame.bottom;
            auto picture = std::make_unique<uint8_t[]>(picture_size);
            std::memset(picture.get(), 0, picture_size);

            RETURN_IF_ERROR(
                graphics::ParsePICTv1(pict_data, /*output=*/picture.get()));

            RETURN_IF_ERROR(WithType<GrafPort>(
                TRY(port::GetThePort()), [&](const GrafPort& port) {
                  screen.CopyBits(picture.get(), pict_frame, pict_frame,
                                  port::LocalToGlobal(port, header.box));
                  return absl::OkStatus();
                }));
            break;
          }
          case iconItem: {
            reader.SkipNext(1);
            Integer resource_id = TRY(reader.Next<Integer>());
            Handle handle =
                ResourceManager::the().GetResource('ICON', resource_id);
            Ptr icon = TRY(memory::kSystemMemory.Read<Handle>(handle));
            RETURN_IF_ERROR(WithType<GrafPort>(
                TRY(port::GetThePort()), [&](const GrafPort& port) {
                  screen.CopyBits(memory::kSystemMemory.raw_ptr() + icon,
                                  NewRect(0, 0, 32, 32), NewRect(0, 0, 32, 32),
                                  port::LocalToGlobal(port, header.box));
                  return absl::OkStatus();
                }));
            break;
          }
          default:
            LOG(WARNING) << "Unsupported ItemType: "
                         << ItemType_Name(item_type);
        }

        return NEXT_ITERATION;
      });
}

}  // namespace

absl::StatusOr<DialogPtr> GetNewDialog(Integer dialog_id,
                                       Ptr d_storage,
                                       WindowPtr behind) {
  // If NULL is passed as `d_storage`, allocate space for the record.
  if (d_storage == 0) {
    d_storage = MemoryManager::the().Allocate(DialogRecord::fixed_size);
  }

  Handle dialog_handle = ResourceManager::the().GetResource('DLOG', dialog_id);
  DLOG dialog_resource =
      TRY(MemoryManager::the().ReadTypeFromHandle<DLOG>(dialog_handle));
  LOG(INFO) << "DLOG: " << dialog_resource;

  DialogRecord dialog_record;

  dialog_record.items =
      ResourceManager::the().GetResource('DITL', dialog_resource.item_list_id);

  dialog_record.window_record = TRY(WindowManager::the().NewWindowRecord(
      dialog_resource.initial_rect, dialog_resource.title,
      dialog_resource.is_visible, dialog_resource.has_close,
      dialog_resource.window_definition_id, behind,
      dialog_resource.reference_constant));

  // Manually adjust the `windowKind` to match `dialogKind`
  dialog_record.window_record.window_kind = kDialogKind;

  RETURN_IF_ERROR(WriteType<DialogRecord>(std::move(dialog_record),
                                          memory::kSystemMemory, d_storage));

  // Logic from `GetNewWindow` needs to be replicated here.
  if (dialog_record.window_record.is_visible) {
    RETURN_IF_ERROR(WindowManager::the().ShowWindow(d_storage));
  }

  // NewWindow calls OpenPort which "makes that graphics port the current port
  // (by calling SetPort)" so we must do that here.
  // Reference: https://dev.os9.ca/techpubs/mac/QuickDraw/QuickDraw-32.html
  RETURN_IF_ERROR(port::SetThePort(d_storage +
                                   DialogRecordFields::window_record.offset +
                                   WindowRecordFields::port.offset));

  WindowManager::the().AddWindowToListAndActivate(d_storage);
  return d_storage;
}

absl::Status GetDialogItem(DialogPtr the_dialog,
                           Integer target_item_no,
                           Var<Integer> item_type,
                           Var<Handle> item,
                           Var<Rect> box) {
  const DialogRecord dialog_record =
      TRY(ReadType<DialogRecord>(memory::kSystemMemory, the_dialog));

  core::MemoryRegion item_memory =
      MemoryManager::the().GetRegionForHandle(dialog_record.items);

  return IterateItems(
      item_memory,
      [&](Integer item_no, size_t offset) -> absl::StatusOr<IterationControl> {
        if (target_item_no != item_no)
          return NEXT_ITERATION;

        ItemHeader header = TRY(ReadType<ItemHeader>(item_memory, offset));

        RETURN_IF_ERROR(memory::kSystemMemory.Write<Integer>(
            item_type.ptr, header.type_and_disabled));
        // The Handle is located in the first 4 bytes (a pointer to an object).
        RETURN_IF_ERROR(memory::kSystemMemory.Write<Handle>(item.ptr, offset));
        RETURN_IF_ERROR(
            WriteType<Rect>(header.box, memory::kSystemMemory, box.ptr));
        return STOP_ITERATION;
      });
}

absl::Status SetDialogItem(DialogPtr the_dialog,
                           Integer target_item_no,
                           Integer item_type,
                           Handle item,
                           Rect box) {
  const DialogRecord dialog_record =
      TRY(ReadType<DialogRecord>(memory::kSystemMemory, the_dialog));

  core::MemoryRegion item_memory =
      MemoryManager::the().GetRegionForHandle(dialog_record.items);

  return IterateItems(
      item_memory,
      [&](Integer item_no, size_t offset) -> absl::StatusOr<IterationControl> {
        if (target_item_no != item_no)
          return NEXT_ITERATION;

        ItemHeader new_header;
        new_header.item = item;
        new_header.box = box;
        new_header.type_and_disabled = item_type;

        RETURN_IF_ERROR(
            WriteType<ItemHeader>(std::move(new_header), item_memory, offset));
        return STOP_ITERATION;
      });

  return absl::OkStatus();
}

absl::StatusOr<bool> IsDialogEvent(const EventRecord event_record) {
  // From: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-427.html
  // A dialog record includes a window record. When you use the GetNewDialog,
  // NewDialog, or NewColorDialog function to create a dialog box, the Dialog
  // Manager sets the windowKind field in the window record to dialogKind. To
  // determine whether the active window is a dialog box, IsDialogEvent checks
  // the windowKind field.

  Ptr front_window = WindowManager::the().GetFrontWindow();
  // No windows active.
  if (front_window == 0) {
    return false;
  }

  WindowRecord window_record =
      TRY(ReadType<WindowRecord>(memory::kSystemMemory, front_window));

  return window_record.window_kind == kDialogKind;
}

// Link: https://dev.os9.ca/techpubs/mac/Toolbox/Toolbox-426.html
absl::Status ModalDialog(Ptr filter_proc, Var<Integer> item_hit) {
  CHECK(filter_proc == 0) << "Custom `filter_proc` not yet supported.";

  while (true) {
    EventRecord event = EventManager::the().GetNextEvent(0xFFFF);
    if (event.what == kNullEvent)
      break;

    switch (event.what) {
      case kWindowUpdate:
        RETURN_IF_ERROR(DrawDialogWindow(event.message));
        break;
    }
  }

  LOG(INFO) << "FrontWindow: " << std::hex
            << WindowManager::the().GetFrontWindow();
  DialogRecord record = TRY(ReadType<DialogRecord>(
      memory::kSystemMemory, WindowManager::the().GetFrontWindow()));

  CHECK_EQ(record.window_record.window_kind, kDialogKind)
      << "Passed WindowRecord must be a dialogKind";

  core::MemoryRegion item_memory =
      MemoryManager::the().GetRegionForHandle(record.items);

  auto mouse_move_enabler = EventManager::the().EnableMouseMove();
  while (true) {
    EventRecord record = EventManager::the().GetNextEvent(1 << kMouseDown);
    auto test_control_selected =
        [&](Integer item_no,
            size_t offset) -> absl::StatusOr<IterationControl> {
      ItemHeader header = TRY(ReadType<ItemHeader>(item_memory, offset));

      bool is_disabled = header.type_and_disabled & ItemType::itemDisable;
      Integer item_type = header.type_and_disabled & 0x7f;

      if (is_disabled)
        return NEXT_ITERATION;
      if (item_type != btnCtrl)
        return NEXT_ITERATION;

      if (PointInRect(record.where,
                      TRY(port::ConvertLocalToGlobal(header.box)))) {
        RETURN_IF_ERROR(
            memory::kSystemMemory.Write<Integer>(item_hit.ptr, item_no));
        return STOP_ITERATION;
      }
      return NEXT_ITERATION;
    };

    if (record.what == kMouseDown) {
      absl::Status status =
          IterateItems(item_memory, std::move(test_control_selected));
      CHECK(status.ok()) << "IterateItems failed: " << std::move(status);
      break;
    }
  }

  return absl::OkStatus();
}

}  // namespace cyder::dialog