#!/usr/bin/env python3
#
# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import sys

from compiler.codegen.codegen import CodeGenerator
from compiler.error_message import print_errors
from compiler.files import FileResolver, FileException
from compiler.type_checker import TypeChecker


def compile(files_to_compile, output_path, root_directory):
  try:
    sorted_files = FileResolver(root_directory).resolve(files_to_compile)
  except FileException as e:
    print_errors(e.errors, e.contents)
    exit(-1)

  global_checked_exprs = []
  for file in sorted_files:
    (exprs, errors) = TypeChecker().check(global_checked_exprs, file.exprs)
    if errors:
      print_errors(errors, file.contents)
      sys.exit(-1)

    global_checked_exprs += exprs
    if file.path not in files_to_compile:
      continue

    if errors := CodeGenerator(exprs).generate(
        file.includes, output_path
    ):
      for error in errors:
        print(error)
      sys.exit(-1)

def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("-i", "--input", nargs="+",
                      help="Type definition file(s) to compile")
  parser.add_argument(
      "-o", "--output", type=str, help="Path to output the C++ code (without extensions)"
  )
  parser.add_argument(
      "root_directory", type=str, help="Path which all @include(s) are relative to"
  )
  args = parser.parse_args()

  compile(args.input, args.output, args.root_directory)


if __name__ == "__main__":
  main()
