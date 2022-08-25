from __future__ import annotations

from dataclasses import dataclass

from typing import List, Tuple, Union
from tokenizer import Token


def merge_span(start_span, end_span):
  """Create a span that spans `start_span` to `end_span`"""
  return (start_span[0], end_span[1])


@dataclass
class TypeExpression:
  id: str
  span: Tuple[int, int]


@dataclass
class AssignExpression:
  id: str
  type: Union[TypeExpression, ArrayTypeExpression]
  span: Tuple[int, int]


@dataclass
class StructExpression:
  id: str
  members: List[AssignExpression]
  span: Tuple[int, int]


@dataclass
class ArrayTypeExpression:
  inner_type: TypeExpression
  length_label: str
  include_length: bool
  span: Tuple[int, int]


@dataclass
class GarbageExpression:
  span: Tuple[int, int]


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

  def _error(self, message) -> GarbageExpression:
    error_token = self._current
    self._errors.append((message, self._current.span))
    self._advance()
    return GarbageExpression(error_token.span)

  def _parse_type_expression(self):
    if self._current.type == Token.Type.START_SQUARE_BRACKET:
      return self._parse_array_type_expression()

    start_span = self._current.span
    if self._current.type != Token.Type.IDENTIFIER:
      return self._error('missing type')

    label_expr = self._current
    self._advance()

    if self._current.type != Token.Type.SEMICOLON:
      return self._error('missing ";"')

    expr_span = merge_span(start_span, self._current.span)
    self._advance()
    return TypeExpression(label_expr.label, expr_span)

  def _parse_array_type_expression(self) -> Union[ArrayTypeExpression, GarbageExpression]:
    assert self._current.type == Token.Type.START_SQUARE_BRACKET
    start_span = self._current.span
    self._advance()

    inner_type = self._parse_type_expression()

    if not isinstance(inner_type, TypeExpression):
      return self._error('loop type must be a struct or primitive')

    include_length = False
    condition_span = None
    if self._current.type == Token.Type.LESS_THAN:
      condition_span = self._current.span
      self._advance()

      if self._current.type == Token.Type.EQUAL_TO:
        condition_span = merge_span(condition_span, self._current.span)
        include_length = True
        self._advance()

    length_label = None
    if self._current.type == Token.Type.IDENTIFIER:
      length_label = self._current.label
      self._advance()
    elif self._current.type == Token.Type.NULL:
      if condition_span != None:
        self._errors.append(
          ('loop conditions not allowed with "null"', condition_span))

      length_label = 'null'
      self._advance()
    else:
      return self._error('expected length of array')

    if self._current.type != Token.Type.END_SQUARE_BRACKET:
      return self._error('missing "]"')
    self._advance()

    if self._current.type != Token.Type.SEMICOLON:
      return self._error('missing ";"')

    expr_span = merge_span(start_span, self._current.span)
    self._advance()

    return ArrayTypeExpression(inner_type, length_label, include_length, expr_span)

  def _parse_assignement(self):
    start_span = self._current.span
    if self._current.type != Token.Type.IDENTIFIER:
      return self._error('missing label')

    label_token = self._current
    self._advance()

    if self._current.type != Token.Type.COLON:
      return self._error('missing ":"')
    self._advance()

    type_expr = self._parse_type_expression()
    expr_span = merge_span(start_span, type_expr.span)

    if isinstance(type_expr, GarbageExpression):
      return type_expr

    return AssignExpression(label_token.label, type_expr, expr_span)

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
      if isinstance(result, GarbageExpression):
        return result

      members.append(result)

    if self._current.type != Token.Type.END_CURLY_BRACKET:
      return self._error('missing "}')

    expr_span = merge_span(start_span, self._current.span)
    self._advance()

    return StructExpression(label_token.label, members, expr_span)

  def _parse_type(self):
    assert(self._current.type == Token.Type.TYPE)
    self._advance()

    result = self._parse_assignement()
    if isinstance(result, GarbageExpression):
      return result

    return result

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
