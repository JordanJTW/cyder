from enum import Enum, auto
from typing import List, Tuple


class Token:
  class Type(Enum):
    TYPE = auto()
    STRUCT = auto()
    IDENTIFIER = auto()
    COLON = auto()
    SEMICOLON = auto()
    AT_SIGN = auto()
    START_CURLY_BRACKET = auto()
    END_CURLY_BRACKET = auto()
    START_SQUARE_BRACKET = auto()
    END_SQUARE_BRACKET = auto()
    END_OF_FILE = auto()
    GARBAGE = auto()

  @property
  def span(self):
    return self._span

  @property
  def type(self):
    return self._type

  def __init__(self, type: Type, span: Tuple, text: str = None):
    self._type = type
    self._span = span
    self._text = text

  def __str__(self) -> str:
    if self._text:
      return f'Token(\n  type: {self._type}\n  span: {self._span}\n  text: "{self._text}"\n)'

    return f'Token(\n  type: {self._type}\n  span: {self._span}\n)'


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

    return Token(Token.Type.IDENTIFIER, span, name)

  def _consume_comment(self):
    # Consume '//'
    self._index = self._index + 2

    while not self._is_eof():
      if self._current == '\n':
        break

      self._advance()

  def _parse_name(self):
    type_name = ''
    start_index = self._index
    while not self._is_eof():
      if not (self._current.isalnum() or self._current == '_'):
        break

      type_name = type_name + self._current
      self._advance()

    return self._get_token_for_type_name(type_name, (start_index, self._index))

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
        tokens.append(self._parse_name())
        continue

      char_span = (self._index, self._index + 1)

      if self._current == ':':
        tokens.append(Token(Token.Type.COLON, char_span))
        self._advance()
        continue

      if self._current == ';':
        tokens.append(Token(Token.Type.SEMICOLON, char_span))
        self._advance()
        continue

      if self._current == '{':
        tokens.append(Token(Token.Type.START_CURLY_BRACKET, char_span))
        self._advance()
        continue

      if self._current == '}':
        tokens.append(Token(Token.Type.END_CURLY_BRACKET, char_span))
        self._advance()
        continue

      if self._current == '[':
        tokens.append(Token(Token.Type.START_SQUARE_BRACKET, char_span))
        self._advance()
        continue

      if self._current == ']':
        tokens.append(Token(Token.Type.END_SQUARE_BRACKET, char_span))
        self._advance()
        continue

      if self._current == '@':
        tokens.append(Token(Token.Type.AT_SIGN, char_span))
        self._advance()
        continue

      if self._current == '/':
        if self._contents[self._index + 1] != '/':
          tokens.append(
            Token(Token.Type.GARBAGE, char_span, text=self._current))
          errors.append(('Should this be // for a comment?', char_span))
          self._advance()
          continue

        self._consume_comment()
        continue

      tokens.append(Token(Token.Type.GARBAGE, char_span, text=self._current))
      errors.append(('Unknown character found in file', char_span))
      self._advance()

    return (tokens, errors)
