#!/usr/bin/env python3

import argparse
import sys

from compiler.codegen import CodeGenerator
from compiler.error_message import print_errors
from compiler.parser import Parser
from compiler.tokenizer import Tokenizer
from compiler.type_checker import TypeChecker


def compile(filename, output_path):
  with open(filename, 'r') as f:
    contents = f.read()

    tokenizer = Tokenizer(contents)
    (tokens, errors) = tokenizer.generate_tokens()

    if errors:
      print_errors(errors, contents)
      sys.exit(-1)

    parser = Parser(tokens)
    (exprs, errors) = parser.parse()

    if errors:
      print_errors(errors, contents)
      sys.exit(-1)

    (checked_exprs, errors) = TypeChecker().check(exprs)

    if errors:
      print_errors(errors, contents)
      sys.exit(-1)

    codegen = CodeGenerator(checked_exprs)
    if errors := codegen.generate(output_path):
      for error in errors:
        print(error)
      sys.exit(-1)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument(
    'input', type=str, help='Type definition file to compile')
  parser.add_argument('output', type=str,
                      help='Path to output the C++ code (without extensions)')
  args = parser.parse_args()

  compile(args.input, args.output)


if __name__ == '__main__':
  main()
