#!/usr/bin/env python3

import os
import re
import sys

GLOBAL_NAMES_PATH = os.path.join(os.path.dirname(
    os.path.realpath(__file__)), 'global_names.txt')

GLOBAL_NAMES_ENTRY = re.compile(
    r'^(?P<name>[\*\?\w\[\]]+)\s+(?P<address>[\d\w]{4})\s+(?P<size>byte|word|long|\d+|-)(?P<description>.+)')


HEADER = """
#pragma once

#include <cstdint>

const char* GetGlobalVarName(uint32_t address);

"""

CC_HEADER = """
// Auto-generated by: //gen/main.py
#include "trap_names.h"

#include <iomanip>

#include "core/logging.h"
"""

SWITCH_CASE = """
    case {}: return "{}";
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

    address_to_metadata = {}
    error_lines = []
    with open(GLOBAL_NAMES_PATH, 'r') as f:
        for count, line in enumerate(f.readlines()):
            # Skip blank lines
            if not line.strip():
                continue

            match = GLOBAL_NAMES_ENTRY.match(line)
            if match := GLOBAL_NAMES_ENTRY.match(line):
                name = match.group('name')
                address = match.group('address')
                size = match.group('size')
                description = match.group('description')

                if size == 'byte':
                    size = 1
                elif size == 'word':
                    size = 2
                elif size == 'long':
                    size = 4

                address = int(address, 16)

                metadata = address_to_metadata.get(address, [])
                metadata.append({'name': name, 'size': size,
                                'description': description})
                address_to_metadata[address] = metadata

            else:
                error_lines.append((count + 1, line))

    if error_lines:
        for (count, line) in error_lines:
            print(f'Error line {count}: "{line.strip()}"')
        sys.exit(-2)

    # Write the .h
    with open(h_path, 'w') as f:
        f.write(HEADER)

        f.write('enum GlobalVars {')
        for address, metadata in address_to_metadata.items():
            names = set((item['name'].removeprefix('*')
                        for item in metadata if item['name'] != '[????]'))
            for name in names:
                f.write(f"\n  {name} = 0x{address:X},")
        f.write('\n};')

    # Write the .cc
    with open(cc_path, 'w') as f:
        f.write(CC_HEADER)

        # GetTrapName:
        f.write(
            """
const char* GetGlobalVarName(uint32_t address) {
  switch (address) {""")

        for address, metadata in address_to_metadata.items():
            names = set((f"{item['name'].removeprefix('*')}[{item['size']}]"
                        for item in metadata if item['name'] != '[????]'))

            if not names:
                continue

            f.write(SWITCH_CASE.format(
                f'0x{address:X}', f"{'|'.join(names)}"))

        f.write("""
    default:
      NOTREACHED() << "Unknown global variable address: 0x" << std::hex << address;
      return nullptr; 
  }
}""")


if __name__ == '__main__':
    main()