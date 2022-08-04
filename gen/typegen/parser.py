import json

from enum import Enum, auto
from tokenizer import Token


class Expression:
  class Type(Enum):
    TYPE = auto()
    STRUCT = auto()
    GARBAGE = auto()

  def __init__(self, type, span, data=None):
    self._type = type
    self._span = span
    self._data = data

  @property
  def data(self):
    return self._data

  @property
  def span(self):
    return self._span

  @property
  def type(self):
    return self._type

  class _Encoder(json.JSONEncoder):
    """JSON encoder to handle printing Type(Enum) in __repr__"""

    def default(self, obj):
      if isinstance(obj, Expression):
        (start_span, end_span) = obj.span
        return {'type': str(obj._type), 'span': f'{start_span}-{end_span}', 'data': obj._data}

      return obj

  def __repr__(self) -> str:
    return json.dumps(self, cls=Expression._Encoder, indent=2)


def merge_span(start_span, end_span):
  """Create a span that spans `start_span` to `end_span`"""
  return (start_span[0], end_span[1])


class Parser:
  def __init__(self, tokens):
    self._tokens = tokens
    self._index = 0
    self._errors = []

  def _is_eof(self):
    return self._index >= len(self._tokens) or self._current.type == Token.Type.END_OF_FILE

  def _advance(self):
    self._index = self._index + 1

  @property
  def _current(self) -> Token:
    return self._tokens[self._index]

  def _error(self, message) -> Expression:
    error_token = self._current
    self._errors.append((message, self._current.span))
    self._advance()
    return Expression(Expression.Type.GARBAGE, error_token.span)

  def _parse_assignement(self):
    start_span = self._current.span

    if self._current.type != Token.Type.IDENTIFIER:
      return self._error('missing label')

    label_token = self._current
    self._advance()

    if self._current.type != Token.Type.COLON:
      return self._error('missing ":"')
    self._advance()

    if self._current.type == Token.Type.START_SQUARE_BRACKET:
      self._advance()

      if self._current.type != Token.Type.IDENTIFIER:
        return self._error('expected type of array')

      array_type_token = self._current
      self._advance()

      if self._current.type != Token.Type.SEMICOLON:
        return self._error('missing ";"')
      self._advance()

      variable_length = False
      if self._current.type == Token.Type.AT_SIGN:
        variable_length == True
        self._advance()

      if self._current.type != Token.Type.IDENTIFIER:
        return self._error('expected length of array')

      array_length_token = self._current
      self._advance()

      if self._current.type != Token.Type.END_SQUARE_BRACKET:
        return self._error('missing "]"')
      self._advance()

      if self._current.type != Token.Type.SEMICOLON:
        return self._error('missing ";"')

      end_span = self._current.span
      self._advance()

      return (label_token._text, {'type': array_type_token._text, 'length': array_length_token._text, 'variable': variable_length}, merge_span(start_span, end_span))

    if self._current.type != Token.Type.IDENTIFIER:
      return self._error('missing type')

    type_token = self._current
    self._advance()

    if self._current.type != Token.Type.SEMICOLON:
      return self._error('missing ";"')

    end_span = self._current.span
    self._advance()
    return (label_token._text, type_token._text, merge_span(start_span, end_span))

  def _parse_struct(self):
    start_span = self._current.span

    assert(self._current.type == Token.Type.STRUCT)
    self._advance()

    if self._current.type != Token.Type.IDENTIFIER:
      return self._error('missing struct label')

    label_token = self._current
    self._advance()

    if self._current.type != Token.Type.START_CURLY_BRACKET:
      return self._error('missing "{"')
    self._advance()

    members = []
    while not self._is_eof():
      if self._current.type == Token.Type.END_CURLY_BRACKET:
        break

      result = self._parse_assignement()
      if isinstance(result, Expression):
        return result

      (label, type, _) = result
      members.append({'name': label, 'type': type})

    if self._current.type != Token.Type.END_CURLY_BRACKET:
      return self._error('missing "}')

    end_span = self._current.span
    self._advance()

    return Expression(
      Expression.Type.STRUCT,
      merge_span(start_span, end_span),
      data={
        'label': label_token._text,
        'members': members
      }
    )

  def _parse_type(self):
    start_span = self._current.span

    assert(self._current.type == Token.Type.TYPE)
    self._advance()

    result = self._parse_assignement()
    if isinstance(result, Expression):
      return result

    (label, type, span) = result

    return Expression(
        Expression.Type.TYPE,
        merge_span(start_span, span),
        data={
          'label': label,
          'type': type
        })

  def parse(self):
    expressions = []
    while not self._is_eof():
      if self._current.type == Token.Type.TYPE:
        expressions.append(self._parse_type())
        continue

      if self._current.type == Token.Type.STRUCT:
        expressions.append(self._parse_struct())
        continue

      self._errors.append(('unknown start of expression', self._current.span))
      self._advance()

    return (expressions, self._errors)
