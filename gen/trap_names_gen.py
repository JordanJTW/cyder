#!/usr/bin/env python3
#
# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

import os
import re
import sys

TRAP_NAMES_PATH = os.path.join(os.path.dirname(
    os.path.realpath(__file__)), 'trap_names.txt')

RE_NAME_ENTRY = re.compile(r'^(?P<trap>A[\d\w]{3}) => (?P<name>[\d\w]+)$')

HEADER = """
#pragma once

#include <cstdint>

const char* GetTrapName(uint16_t trap);

const char* GetTrapNameByToolboxIndex(uint16_t index);

const char* GetTrapNameBySystemIndex(uint16_t index);

"""

CC_HEADER = """
// Auto-generated by: //gen/trap_names_gen.py
#include "trap_names.h"

#include <iomanip>

#include "core/logging.h"
""".strip()

SWITCH_CASE = """
    // {}
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

    traps = {'TB': {}, 'OS': {}}
    for entry in match_lines:
        (name, trap) = (entry.group('name'), entry.group('trap'))
        trap = int(trap, 16)

        is_toolbox = (trap >> 11) & 1
        category = 'TB' if is_toolbox else 'OS'
        index = (trap & 0x03FF) if is_toolbox else (trap & 0x00FF)

        if index not in traps[category]:
            traps[category][index] = []
        elif category == 'TB':
            print('Duplicate index found for Toolbox trap!')
            sys.exit(-1)

        if category == 'OS':
            flags = (trap >> 9) & 0x03
            name = f'{name}[{flags:02b}]'

        traps[category][index].append(name)

    # Remove [00] flags from the end of traps with unique indexs
    for index, names in traps['OS'].items():
        if len(names) > 1:
            continue

        if not names[0].endswith('[00]'):
            print('Expected unique OS trap to have [00] flag')
            sys.exit(-1)

        traps['OS'][index] = [names[0].replace('[00]', '')]

    # Write the .h
    with open(h_path, 'w') as f:
        f.write(HEADER)

        f.write('enum Trap {')
        for entry in match_lines:
            f.write(f"\n  {entry.group('name')} = 0x{entry.group('trap')},")
        f.write('\n};')

    # Write the .cc
    with open(cc_path, 'w') as f:
        f.write(CC_HEADER)

        # GetTrapName:
        f.write(
            """
const char* GetTrapName(uint16_t trap) {
  switch (trap) {""")

        for entry in match_lines:
            f.write(SWITCH_CASE.format(
                entry.group(0), '0x{}'.format(entry.group('trap')), entry.group('name')))

        f.write("""
    default:
      NOTREACHED() << "Unknown trap encountered: 0x" << std::hex << trap;
      return nullptr; 
  }
}""")

        # GetTrapNameByToolboxIndex:
        f.write(
            """
const char* GetTrapNameByToolboxIndex(uint16_t index) {
  switch (index) {""")

        for index, names in traps['TB'].items():
            f.write(SWITCH_CASE.format(
                'Toolbox', index, '|'.join(names)))

        f.write("""
    default:
      NOTREACHED() << "Unknown toolbox index encountered: " << index;
      return nullptr; 
  }
}""")

        # GetTrapNameBySystemIndex:
        f.write(
            """
const char* GetTrapNameBySystemIndex(uint16_t index) {
  switch (index) {""")

        for index, names in traps['OS'].items():
            f.write(SWITCH_CASE.format(
                'System', index, '|'.join(names)))

        f.write("""
    default:
      NOTREACHED() << "Unknown system index encountered: " << index;
      return nullptr; 
  }
}""")


if __name__ == '__main__':
    main()
