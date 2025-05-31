#!/usr/bin/env python3
#
# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import os
import sys

from compiler.codegen import CodeGenerator
from compiler.error_message import print_errors
from compiler.files import File, FileResolver
from compiler.type_checker import TypeChecker


def compile(filename, output_path, root_directory):
  file_to_compile = File.open_file(filename)
  sorted_files = FileResolver(root_directory).resolve(file_to_compile)

  global_checked_exprs = []
  root_file_exprs = []
  for file in sorted_files:
    (exprs, errors) = TypeChecker().check(global_checked_exprs, file.exprs)
    if errors:
      print_errors(errors, file.contents)
      sys.exit(-1)

    global_checked_exprs += exprs
    # The last file processed should be `file_to_compile` due to the DFS
    root_file_exprs = exprs

  if errors := CodeGenerator(root_file_exprs).generate(
      file_to_compile.includes, output_path
  ):
    for error in errors:
      print(error)
    sys.exit(-1)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument("input", type=str,
                      help="Type definition file to compile")
  parser.add_argument(
      "output", type=str, help="Path to output the C++ code (without extensions)"
  )
  parser.add_argument(
      "root_directory", type=str, help="Path which all @include(s) are relative to"
  )
  args = parser.parse_args()

  compile(args.input, args.output, args.root_directory)


if __name__ == "__main__":
  main()
