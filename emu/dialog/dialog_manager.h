
#pragma once

#include "emu/base_types.h"
#include "emu/window_manager.h"
#include "third_party/abseil-cpp/absl/status/statusor.h"

namespace cyder::dialog {

using DialogPtr = Ptr;

absl::StatusOr<DialogPtr> GetNewDialog(Integer dialog_id,
                                       Ptr d_storage,
                                       WindowPtr behind);

absl::Status GetDialogItem(DialogPtr the_dialog,
                           Integer item_no,
                           Var<Integer> item_type,
                           Var<Handle> item,
                           Var<Rect> box);

absl::Status SetDialogItem(DialogPtr the_dialog,
                           Integer item_no,
                           Integer item_type,
                           Handle item,
                           Rect box);

absl::StatusOr<bool> IsDialogEvent(const EventRecord event_record);

absl::Status ModalDialog(Ptr filter_proc, Var<Integer> item_hit);

}  // namespace cyder::dialog
