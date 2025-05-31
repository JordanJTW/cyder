import os

from dataclasses import dataclass
from typing import List, Set, Tuple

from compiler.tokenizer import Tokenizer
from compiler.type_parser import Parser


@dataclass
class FileException(Exception):
  contents: str
  errors: List[Tuple]


class File:
  def __init__(self, path: str, contents, includes: List[str], exprs):
    self.path = path
    self.contents = contents
    self.includes = includes
    self.exprs = exprs

  def __eq__(self, file):
    return self.path == file.path

  @staticmethod
  def open_file(path):
    with open(path, 'r') as f:
      contents = f.read()

      tokenizer = Tokenizer(contents)
      (tokens, errors) = tokenizer.generate_tokens()

      if errors:
        raise FileException(contents, errors)

      parser = Parser(tokens)
      (parsed_exprs, include_paths, errors) = parser.parse()

      if errors:
        raise FileException(contents, errors)

      return File(path, contents, include_paths, parsed_exprs)


class FileResolver:
  def __init__(self, root_directory: str):
    self.root_directory = root_directory
    self.visited: Set[str] = set()
    self.resolving: Set[str] = set()
    self.result: List[File] = []

  def resolve(self, root: File):
    self._dfs(root)
    return self.result

  def _dfs(self, file: File):
    if file.path in self.resolving:
      raise ValueError(f"Circular dependency detected at '{file.path}'")
    if file.path in self.visited:
      return

    self.resolving.add(file.path)
    for dep in file.includes:
      dep_file = File.open_file(os.path.join(self.root_directory, dep))
      self._dfs(dep_file)
    self.resolving.remove(file.path)

    self.visited.add(file.path)
    self.result.append(file)
