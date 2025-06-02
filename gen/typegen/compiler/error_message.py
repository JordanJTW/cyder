# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

from typing import List, Tuple


def calculate_line_spans(contents: str):
  """Generates a list of tuples representing the start and end
     of each line within `contents`"""
  start_of_line = 0
  line_spans = []
  for index, char in enumerate(contents):
    if char == '\n':
      line_spans.append((start_of_line, index))
      start_of_line = index + 1

  line_spans.append((start_of_line, len(contents)))
  return line_spans


def print_message_for_token(contents: str, span: Tuple, message: str):
  """Prints `message` under the line from `contents` which contains `span`"""
  line_spans = calculate_line_spans(contents)

  extracted_lines = []
  for line, line_span in enumerate(line_spans):
    (line_start, line_end) = line_span
    (span_start, span_end) = span

    if line_start >= span_end:
      break

    if line_end > span_start:
      extracted_lines.append((line, line_start, contents[line_start:line_end]))

  if len(extracted_lines) == 0:
    raise Exception(f'Failed to find line(s) for span: {span}')

  for (idx, _, line) in extracted_lines:
    line_leader = f'Line {idx + 1}:'
    print(line_leader, line)

  message_spacing = len(line_leader) + (span[0] - extracted_lines[0][1])
  print(' ' * message_spacing, '^', message)


def print_errors(errors: List[Tuple], contents: str):
  for error in errors:
    (message, span) = error
    print_message_for_token(contents, span, message)
