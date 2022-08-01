#!/usr/bin/env python3

import argparse
import sys

from error_message import print_message_for_token
from tokenizer import Tokenizer


def compile(filename):
  with open(filename, 'r') as f:
    contents = f.read()

    tokenizer = Tokenizer(contents)
    (tokens, errors) = tokenizer.generate_tokens()

    if errors:
      for error in errors:
        (message, span) = error
        print_message_for_token(contents, span, message)

      sys.exit(-1)

    for token in tokens:
      print(token)


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('sources', nargs='*')
  args = parser.parse_args()

  for source in args.sources:
    compile(source)


if __name__ == '__main__':
  main()
