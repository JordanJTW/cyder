#!/usr/bin/env python3

from pathlib import Path

import argparse
import subprocess
import sys
from typing import Any, Optional, Tuple

PARSER = argparse.ArgumentParser()
COMMANDS = PARSER.add_subparsers()

ROOT_PATH = Path(__file__).absolute().parent
BUILD_PATH = ROOT_PATH.joinpath('build')

# Global flags:
PARSER.add_argument('-f', '--dry_run', action='store_true',
                    help='do not actually run commands')
PARSER.add_argument('-v', '--verbose', action='store_true',
                    help='print all commands being executed')
PARSER.add_argument('-r', '--release', action='store_true',
                    help='build/run release binaries')


def Command(func):
    parser = COMMANDS.add_parser(func.__name__,
                                 help=func.__doc__,
                                 description=func.__doc__)
    for arg in reversed(getattr(func, '__args__', [])):
        parser.add_argument(*arg.name_or_flags, **arg.kwargs)
    parser.set_defaults(func=getattr(func, '__shadow_func__', func))
    return func


class Argument(object):
    def __init__(self, *name_or_flags, **kwargs):
        self.name_or_flags = name_or_flags
        self.kwargs = kwargs

    def __call__(self, func):
        if not hasattr(func, '__args__'):
            func.__args__ = []
        func.__args__.append(self)
        return func


def parse_args():
    args = PARSER.parse_args()
    args_copy = vars(args).copy()
    if 'func' not in args:
        print(PARSER.print_help())
        sys.exit(0)
    del args_copy['func']
    return (args.func, args_copy)


def run_step(command: str, *args: Tuple[Any],
             verbose: bool = False, dry_run: bool = False,
             cwd: Optional[Path] = None, **kwargs):
    if verbose:
        print('\n🤖 \u001b[38;5;160m{} \u001b[38;5;52m{}\u001b[0m'.format(
            command, ' '.join((str(arg) for arg in args))))
        if cwd:
            print(' ↳ \u001b[38;5;242m{}\u001b[0m'.format(cwd))

    if not dry_run:
        try:
            subprocess.check_call([command, *args], cwd=cwd)
        except subprocess.CalledProcessError as e:
            sys.exit(e.returncode)
        except KeyboardInterrupt:
            return


@Command
@Argument('targets', nargs='*', metavar='TARGET')
@Argument('-c', '--clean', action='store_true', help='remove build directory before building')
def build(targets, clean, **kwargs):
    """Build all targets"""

    release_name = 'Release' if kwargs.get('release', False) else 'Debug'
    build_path = BUILD_PATH.joinpath(release_name)

    print(
        f'🐙 Build {", ".join(f"`{target}`" for target in targets)} in {release_name}')

    if clean:
        run_step('rm', '-rf', build_path, **kwargs)

    if not build_path.exists():
        run_step('mkdir', '-p', build_path, **kwargs)

    if not build_path.joinpath('build.ninja').exists():
        run_step('cmake', '-G', 'Ninja', '-B', build_path,
                 '-D', f'CMAKE_BUILD_TYPE={release_name}', **kwargs)

    run_step(
        'ninja', '-j', '64', '-C', build_path, *targets, **kwargs)


@Command
@Argument('target', metavar='TARGET')
@Argument('args', nargs='*', metavar='ARGS')
def run(target, args, **kwargs):
    """Run executable built from target"""

    release_name = 'Release' if kwargs.get('release', False) else 'Debug'
    binary_path = BUILD_PATH.joinpath(release_name, 'exe', target)

    print(f'🐙 Run `{target}` from {release_name}')

    if not binary_path.exists():
        print('No executable found for target: "{}"'.format(target))
        return

    run_step(binary_path, *args, **kwargs)


@Command
@Argument('target', metavar='TARGET')
@Argument('args', nargs='*', metavar='ARGS')
@Argument('-c', '--clean', action='store_true', help='remove build directory before building')
def bar(target, **kwargs):
    """Build and run a single target"""

    build(targets=[target], **kwargs)
    run(target, **kwargs)


@Command
@Argument('args', nargs='*', metavar='ARGS')
@Argument('-c', '--clean', action='store_true', help='remove build directory before building')
def test(args, **kwargs):
    """Build and run a single target"""

    # Build all targets
    build(targets=[], **kwargs)

    release_name = 'Release' if kwargs.get('release', False) else 'Debug'
    build_path = BUILD_PATH.joinpath(release_name)

    run_step('ctest', *args, cwd=build_path)


def main():
    (func, args) = parse_args()
    func(**args)


if __name__ == '__main__':
    main()
