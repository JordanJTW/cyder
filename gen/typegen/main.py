#!/usr/bin/env python3

import argparse
import sys

from error_message import print_errors
from parser import Parser
from tokenizer import Tokenizer


def compile(filename):
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

    for expr in exprs:
      print(expr)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('sources', nargs='*')
  args = parser.parse_args()

  for source in args.sources:
    compile(source)


if __name__ == '__main__':
  main()
