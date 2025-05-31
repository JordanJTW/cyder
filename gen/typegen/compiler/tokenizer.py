# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

from enum import Enum, auto
from typing import List, Optional, Tuple


class Token:
  class Type(Enum):
    TYPE = auto()
    STRUCT = auto()
    IDENTIFIER = auto()
    NUMBER = auto()
    COLON = auto()
    SEMICOLON = auto()
    START_CURLY_BRACKET = auto()
    END_CURLY_BRACKET = auto()
    START_SQUARE_BRACKET = auto()
    END_SQUARE_BRACKET = auto()
    START_PARENTHESIS = auto()
    END_PARENTHESIS = auto()
    AT = auto()
    STRING = auto()
    END_OF_FILE = auto()
    GARBAGE = auto()

  @property
  def type(self):
    return self._type

  @property
  def span(self):
    return self._span

  @property
  def number(self) -> int:
    assert self.type == Token.Type.NUMBER, 'number is only valid for DIGITS'
    assert self._number != None, 'number must not be None'
    return self._number

  @property
  def label(self) -> str:
    assert self._str != None, 'str must not be None'
    return self._str

  def __init__(self, type: Type, span: Tuple, str: Optional[str] = None, number: Optional[int] = None):
    self._type = type
    self._span = span
    self._str = str
    self._number = number

  def __str__(self) -> str:
    if self.type == Token.Type.IDENTIFIER:
      return f'Token(\n  type: {self.type}\n  span: {self.span}\n  label: "{self.label}"\n)'

    if self.type == Token.Type.NUMBER:
      return f'Token(\n  type: {self.type}\n  span: {self.span}\n  number: "{self.number}"\n)'

    return f'Token(\n  type: {self._type}\n  span: {self._span}\n)'


CHAR_TO_TOKEN_TYPE = {
    ':': Token.Type.COLON,
    ';': Token.Type.SEMICOLON,
    '{': Token.Type.START_CURLY_BRACKET,
    '}': Token.Type.END_CURLY_BRACKET,
    '[': Token.Type.START_SQUARE_BRACKET,
    ']': Token.Type.END_SQUARE_BRACKET,
    '(': Token.Type.START_PARENTHESIS,
    ')': Token.Type.END_PARENTHESIS,
    '@': Token.Type.AT,
}


class Tokenizer:
  def __init__(self, contents: str):
    self._index = 0
    self._contents = contents

  @property
  def _current(self) -> str:
    return self._contents[self._index]

  def _advance(self):
    self._index = self._index + 1

  def _is_whitespace(self) -> bool:
    return self._current in [' ', '\t', '\r', '\f', '\v', '\n']

  def _is_eof(self) -> bool:
    return self._index == len(self._contents)

  def _get_token_for_type_name(self, name: str, span: Tuple) -> Token:
    if name == 'type':
      return Token(Token.Type.TYPE, span)

    if name == 'struct':
      return Token(Token.Type.STRUCT, span)

    return Token(Token.Type.IDENTIFIER, span, str=name)

  def _consume_comment(self):
    # Consume '//'
    self._index = self._index + 2

    while not self._is_eof():
      if self._current == '\n':
        break

      self._advance()

  def _parse_label(self):
    type_name = ''
    start_index = self._index
    while not self._is_eof():
      if not (self._current.isalnum() or self._current == '_'):
        break

      type_name = type_name + self._current
      self._advance()

    return self._get_token_for_type_name(type_name, (start_index, self._index))

  def _parse_number(self):
    number = ''
    start_index = self._index
    while not self._is_eof():
      if not self._current.isdigit():
        break

      number = number + self._current
      self._advance()

    return Token(Token.Type.NUMBER, (start_index, self._index), number=int(number))

  def generate_tokens(self):
    tokens: List[Token] = []
    errors: List[Tuple] = []

    while True:
      if self._is_eof():
        tokens.append(Token(Token.Type.END_OF_FILE,
                      (self._index, self._index)))
        break

      if self._is_whitespace():
        self._advance()
        continue

      if self._current.isalpha():
        tokens.append(self._parse_label())
        continue

      if self._current.isdigit():
        tokens.append(self._parse_number())
        continue

      char_span = (self._index, self._index + 1)

      if token_type := CHAR_TO_TOKEN_TYPE.get(self._current, None):
        tokens.append(Token(token_type, char_span))
        self._advance()
        continue

      if self._current == '"':
        # Ignore initial '"'
        self._advance()
        text = ""
        start_index = self._index
        while not self._is_eof():
          if self._current == '"':
            # Ignore final '"'
            self._advance()
            break

          text += self._current
          self._advance()

        tokens.append(
            Token(Token.Type.STRING, (start_index, self._index), str=text))
        continue

      if self._current == '/':
        if self._contents[self._index + 1] != '/':
          tokens.append(
              Token(Token.Type.GARBAGE, char_span, str=self._current))
          errors.append(('Should this be // for a comment?', char_span))
          self._advance()
          continue

        self._consume_comment()
        continue

      tokens.append(Token(Token.Type.GARBAGE, char_span, str=self._current))
      errors.append(('Unknown character found in file', char_span))
      self._advance()

    return (tokens, errors)
