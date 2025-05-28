#!/usr/bin/env python3
#
# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

import argparse
import os
import sys

from compiler.codegen import CodeGenerator
from compiler.error_message import print_errors
from compiler.tokenizer import Tokenizer
from compiler.type_checker import TypeChecker
from compiler.type_parser import Parser


def parse_file(path):
  with open(path, 'r') as f:
      contents = f.read()

      tokenizer = Tokenizer(contents)
      (tokens, errors) = tokenizer.generate_tokens()

      if errors:
        print_errors(errors, contents)
        sys.exit(-1)

      parser = Parser(tokens)
      (parsed_exprs, include_paths, errors) = parser.parse()

      if errors:
        print_errors(errors, contents)
        sys.exit(-1)
      
      return (parsed_exprs, include_paths)


def process_dependencies(input_file, root):
  includes_to_process = [input_file]
  processed_includes = []
  files_to_process = []

  while len(includes_to_process) != 0:
    path = includes_to_process.pop()
    if path in processed_includes:
      continue

    (parsed_exprs, include_paths) = parse_file(os.path.join(root, path))
    files_to_process.append({'path': path, 'exprs': parsed_exprs, 'includes': set(include_paths)})

    includes_to_process += include_paths
    processed_includes.append(path)
  
  checked_exprs = []
  files_to_process = sorted(files_to_process, key=lambda v: len(v['includes']), reverse=True)
  checked_paths = set()

  return_value = ()
  for _ in range(len(files_to_process)):
    file = files_to_process.pop()

    assert file['includes'].issubset(checked_paths)

    (exprs, errors) = TypeChecker().check(checked_exprs, file['exprs'])

    if errors:
        print('Failed to compile')
        sys.exit(-1)

    checked_exprs += exprs
    checked_paths.add(file['path'])
    return_value = (exprs, file['includes'])
  
  return return_value


def compile(filename, output_path, root):
  filename = os.path.relpath(filename, root)
  (checked_exprs, include_paths) = process_dependencies(filename, root)

  if errors := CodeGenerator(checked_exprs).generate(include_paths, output_path):
    for error in errors:
      print(error)
    sys.exit(-1)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
    'input', type=str, help='Type definition file to compile')
  parser.add_argument('output', type=str,
                      help='Path to output the C++ code (without extensions)')
  parser.add_argument('root', type=str,
                      help='Path which all @include(s) are relative to')
  args = parser.parse_args()

  compile(args.input, args.output, args.root)


if __name__ == '__main__':
  main()
