#!/usr/bin/env python3

import argparse
import os
import re
import subprocess
import sys

from pathlib import Path
from typing import List, Tuple

_TRAP_LINE = re.compile(r'.+TRAP (?P<name>\w+)\(')
_LOAD_SEG_LINE = re.compile(r'.+Load Segment (?P<segment>\d{1,3})')

_ANSI_COLOR_CODE = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
_LOG_PREFIX = re.compile(r'((INFO|WARNING|ERROR|FATAL):[_\w\.]+)\(\d+\):\s')


def parse_status(output_lines: List[str]):
  traps = []
  segments = []
  for line in output_lines:
    if match := _TRAP_LINE.match(line):
      name = match.group('name')
      traps.append(name)
      continue

    if match := _LOAD_SEG_LINE.match(line):
      segment = match.group('segment')
      segments.append(segment)
      continue

  return {
    'traps': traps,
    'segments': segments,
  }


class StatusChecker:
  def __init__(self):
    self._messages: List[Tuple[str, bool]] = []
    self._needs_attention: bool = False
    self._made_progress: bool = False

  def _progress(self, message: str):
    self._messages.append((message, False))
    self._made_progress = True

  def _error(self, message: str):
    self._messages.append((message, True))
    self._needs_attention = True

  def check(self, new_status, base_status) -> List[Tuple[str, bool]]:
    new_trap_count = len(new_status["traps"])
    base_trap_count = len(base_status["traps"])
    trap_delta = new_trap_count - base_trap_count

    if new_trap_count >= base_trap_count:
      trap_percentage = trap_delta / base_trap_count * 100 if base_trap_count else 100
      if trap_delta:
        self._progress(
          f'Executed {trap_delta} more traps ({trap_percentage}%)')

      for idx, base_trap in enumerate(base_status["traps"]):
        new_trap = new_status["traps"][idx]
        if base_trap != new_trap:
          self._error(f'Trap {idx} changed ("{base_trap}" vs. "{new_trap}")')

    else:
      self._error(f'Executed {trap_delta} fewer traps')

      for idx, new_trap in enumerate(new_status["traps"]):
        base_trap = base_status["traps"][idx]
        if base_trap != new_trap:
          self._error(f'Trap {idx} changed ("{base_trap}" vs. "{new_trap}")')

    segment_delta = len(new_status["segments"]) - len(new_status["segments"])

    if segment_delta > 0:
      self._progress(f'Loaded {segment_delta} more segments')
    elif segment_delta < 0:
      self._error(f'Loaded {abs(segment_delta)} fewer segments')

    if self._needs_attention:
      print('😢')
    elif self._made_progress:
      print('🌟')
    else:
      print(f'\u001b[38;5;240mOK\u001b[0m')

    return self._messages


def get_paths(file_or_dir: str):
  if os.path.isdir(file_or_dir):
    return [*Path(file_or_dir).glob('*.rsrc')]

  if os.path.isfile(file_or_dir):
    path = Path(file_or_dir)
    if path.suffix == '.rsrc':
      return [path]

  return []


def sanitize_and_write_log(output: bytes, log_path: Path):
  sanitized = output.decode('UTF-8', errors='ignore')
  sanitized = _ANSI_COLOR_CODE.sub('', sanitized)
  sanitized = _LOG_PREFIX.sub(r'\1: ', sanitized)

  with open(log_path, 'w') as f:
    f.write(sanitized)

  return sanitized


def handle_log_output(output: bytes, app_path: Path, log_dir: Path):
  log_path = log_dir.joinpath(f'{app_path.stem}.output')
  sanitized_logs = sanitize_and_write_log(output, log_path)

  gold_path = app_path.with_suffix('.output')
  if not gold_path.exists():
    print('❓')
    return []

  with open(gold_path, 'r') as f:
    gold_status = parse_status(f.readlines())

  new_status = parse_status(sanitized_logs.splitlines())
  return StatusChecker().check(new_status, gold_status)


def run_tests(file_or_dir: str):
  subprocess.check_call(['./cyder.py', 'build'])

  paths = get_paths(file_or_dir)
  if not paths:
    sys.exit(-1)

  log_dir = Path('/tmp/cyder')
  log_dir.mkdir(exist_ok=True)

  for path in paths:
    print(f'Testing {path.stem}... ', end='')

    messages = []
    try:
      output = subprocess.check_output(
        ['./build/Debug/exe/emu', path], stderr=subprocess.STDOUT, timeout=2)
      messages = handle_log_output(output, path, log_dir)
    except subprocess.CalledProcessError as e:
      messages = handle_log_output(e.output, path, log_dir)
    except subprocess.TimeoutExpired as e:
      messages = handle_log_output(e.output, path, log_dir)

    for message, is_error in messages:
      if is_error:
        print(f'\u001b[38;5;160m{message}\u001b[0m')
      else:
        print(f'\u001b[38;5;240m{message}\u001b[0m')


def main():
  parser = argparse.ArgumentParser()
  parser.add_argument('file_or_dir')
  args = parser.parse_args()

  run_tests(args.file_or_dir)


if __name__ == '__main__':
  main()
