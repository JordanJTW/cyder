#pragma once

#include <bitset>
#include <cstdint>

namespace trap {

// Macintosh Revealed Volume Four
// https://vintageapple.org/macprogramming/pdf/Macintosh_Revealed_Voume_Four_Expanding_the_Toolbox_1990.pdf
//
// Toolbox: 1010 | 1 F F _ | _ _ _ _ | _ _ _ _
// OS:      1010 | 0 F F F | _ _ _ _ | _ _ _ _
//
// F = Flags and _ = Trap Number
//
// NOTE: Generally OS traps pass arguments/return values in registers
// and Toolbox traps pass arguments/return values on the stack.

// Returns if the given trap is a Toolbox trap (or an OS trap)
constexpr bool IsToolbox(uint16_t trap) {
  // Bit 11 determines whether it is an OS or Toolbox trap (see above)
  return (trap >> 11) & 1;
}

// Extracts the 8 or 9-bit (OS/Toolbox) trap index number
constexpr int ExtractIndex(uint16_t trap) {
  // Toolbox traps have 9-bit numbers and OS have 8-bit (see above)
  return IsToolbox(trap) ? (trap & 0x01FF) : (trap & 0x00FF);
}

// Extracts 3 or 2-bit (OS/Toolbox) flags
constexpr std::bitset<3> ExtractFlags(uint16_t trap) {
  return IsToolbox(trap) ? (trap >> 8) & 0x03 : (trap >> 7) & 0x07;
}

}  // namespace trap