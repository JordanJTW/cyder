#pragma once

#include <bitset>
#include <cstdint>

namespace cyder {
namespace trap {

// Link: https://dev.os9.ca/techpubs/mac/OSUtilities/OSUtilities-170.html
//
// Toolbox: 1010 | 1 A _ _ | _ _ _ _ | _ _ _ _
// OS:      1010 | 0 F F R | _ _ _ _ | _ _ _ _
//
// F = Flags, _ = Trap Number, A = Auto-pop bit, R = Return/Save A0
//
// -- Auto-pop bit --
// The auto-pop bit is bit 10 in an A-line instruction for a Toolbox
// routine. Some language systems prefer to generate jump-subroutine
// calls (JSR) to intermediate routines, called glue routines, which
// then call Toolbox routines instead of executing the Toolbox routine
// directly. The auto-pop bit forces the trap dispatcher to remove the
// top 4 bytes from the stack before dispatching to the Toolbox routine.
// After the Toolbox routine completes execution, control is transferred
// back to the place where the glue routine was called from, not back
// to the glue routine.
//
// -- Return/Save A0 --
// Bit R indicates whether the value in register A0 is returned from
// the OS routine. If bit R is 0, the value in register A0 is returned
// from the OS routine. If bit R is 1, the value in register A0 is not
// returned by the Operating System routine.
//
// NOTE: Generally OS traps pass arguments/return values in registers
// and Toolbox traps pass arguments/return values on the stack.

constexpr bool IsToolbox(uint16_t trap) {
  // Bit 11 determines whether it is an OS or Toolbox trap (see above)
  return (trap >> 11) & 1;
}

constexpr bool IsSystem(uint16_t trap) {
  // If a trap is not a Toolbox trap it must be an OS trap (see above)
  return !IsToolbox(trap);
}

// Extracts the 8 or 10-bit (OS/Toolbox) trap index number
constexpr int ExtractIndex(uint16_t trap) {
  // Toolbox traps have 10-bit indexs and OS have 8-bit (see above)
  return IsToolbox(trap) ? (trap & 0x03FF) : (trap & 0x00FF);
}

constexpr bool IsAutoPopSet(uint16_t trap) {
  return IsToolbox(trap) && ((trap >> 10) & 1);
}

std::bitset<2> ExtractFlags(uint16_t trap) {
  CHECK(IsSystem(trap)) << "Only OS traps contain flags";
  return (trap >> 9) & 0x03;
}

constexpr bool ShouldSaveA0(uint16_t trap) {
  return IsSystem(trap) && !((trap >> 8) & 1);
}

}  // namespace trap
}  // namespace cyder