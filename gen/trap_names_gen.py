#!/usr/bin/env python3

import os
import re
import sys

TRAP_NAMES_PATH = os.path.join(os.path.dirname(
    os.path.realpath(__file__)), 'trap_names.txt')

RE_NAME_ENTRY = re.compile(r'^(?P<trap>A[\d\w]{3}) => (?P<name>[\d\w]+)$')

HEADER = """
#pragma once

#include <cstdint>

// Gets the name of the given `trap`
const char* GetTrapName(uint32_t trap);
"""

CC_HEADER = """
// Auto-generated by: //gen/main.py
#include "build/gen/trap_names.h"

#include <iomanip>

#include "core/logging.h"

const char* GetTrapName(uint32_t trap) {
  switch (trap) {
"""

CC_ENTRY = """
    // {}
    case 0x{}: return "{}";
"""

CC_FOOTER = """
    default:
      NOTREACHED() << "Unknown Trap: 0x" << std::hex << trap;
      return nullptr;
  }
}
"""


def parse_args():
    if len(sys.argv) != 2:
        print('Output path required')
        sys.exit(-1)

    output = sys.argv[1]
    print('Saving to:', output + '.{cc,h}')
    cc_path = '{}.cc'.format(output)
    h_path = '{}.h'.format(output)
    return (cc_path, h_path)

def main():
    (cc_path, h_path) = parse_args()

    match_lines = []
    error_lines = []
    with open(TRAP_NAMES_PATH, 'r') as f:
        for line in f.readlines():
            # Skip blank lines
            if not line.strip():
                continue

            match = RE_NAME_ENTRY.match(line)
            if match:
                match_lines.append(match)
            else:
                error_lines.append(line)

    if error_lines:
        for line in error_lines:
            print('Error reading: "{}"'.format(line.strip()))
        sys.exit(-2)

    # Write the .h
    with open(h_path, 'w') as f:
        f.write(HEADER)

    # Write the .cc
    with open(cc_path, 'w') as f:
        f.write(CC_HEADER)
        for entry in match_lines:
            f.write(CC_ENTRY.format(
                entry.group(0), entry.group('trap'), entry.group('name')))
        f.write(CC_FOOTER)


if __name__ == '__main__':
    main()
