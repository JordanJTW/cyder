# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

from __future__ import annotations

from dataclasses import dataclass

from compiler.tokenizer import Token
from typing import List, Tuple, Union, Optional


def merge_span(start_span: Tuple[int, int], end_span: Tuple[int, int]):
  """Create a span that spans `start_span` to `end_span`"""
  return (start_span[0], end_span[1])


@dataclass
class LabelExpression:
  label: str
  span: Tuple[int, int]
  size: Optional[int] = None


@dataclass
class AssignExpression:
  id: LabelExpression
  type: LabelExpression
  span: Tuple[int, int]


@dataclass
class StructExpression:
  id: LabelExpression
  members: List[AssignExpression]
  span: Tuple[int, int]


@dataclass
class ParserException(Exception):
  message: str
  span: Tuple[int, int]


ParsedExpression = Union[StructExpression, AssignExpression]

_ALLOW_SIZE_POSTFIX = ['u8']


class Parser:
  def __init__(self, tokens):
    self._tokens = tokens
    self._index = 0

  def _is_eof(self):
    return self._index >= len(self._tokens) or self._current.type == Token.Type.END_OF_FILE

  def _advance(self):
    self._index = self._index + 1

  @property
  def _current(self) -> Token:
    return self._tokens[self._index]

  # Parses: <identifier>(<[> <digits> <]>);
  def _parse_type_expression(self):
    start_span = self._current.span
    if self._current.type != Token.Type.IDENTIFIER:
      raise ParserException('missing type', self._current.span)

    label_expr = self._current
    self._advance()

    maybe_size = None
    if self._current.type == Token.Type.START_SQUARE_BRACKET:
      if label_expr.label not in _ALLOW_SIZE_POSTFIX:
        raise ParserException('[<size>] only allowed for types: [ ' +
                              ', '.join(_ALLOW_SIZE_POSTFIX) + ' ]', self._current.span)
      self._advance()

      if self._current.type != Token.Type.NUMBER:
        raise ParserException('missing size', self._current.span)

      maybe_size = self._current.number
      self._advance()

      if self._current.type != Token.Type.END_SQUARE_BRACKET:
        raise ParserException('missing "]"', self._current.span)
      self._advance()

    if self._current.type != Token.Type.SEMICOLON:
      raise ParserException('missing ";"', self._current.span)

    expr_span = merge_span(start_span, self._current.span)
    self._advance()
    return LabelExpression(label_expr.label, expr_span, maybe_size)

  def _parse_assignement(self):
    start_span = self._current.span
    if self._current.type != Token.Type.IDENTIFIER:
      raise ParserException('missing label', self._current.span)

    label_token = self._current
    self._advance()

    if self._current.type != Token.Type.COLON:
      raise ParserException('missing ":"', self._current.span)
    self._advance()

    type_expr = self._parse_type_expression()
    expr_span = merge_span(start_span, type_expr.span)

    return AssignExpression(
      LabelExpression(label_token.label, label_token.span),
      type_expr, expr_span)

  def _parse_struct(self):
    start_span = self._current.span

    assert (self._current.type == Token.Type.STRUCT)
    self._advance()

    if self._current.type != Token.Type.IDENTIFIER:
      raise ParserException('missing struct label', self._current.span)

    label_token = self._current
    self._advance()

    if self._current.type != Token.Type.START_CURLY_BRACKET:
      raise ParserException('missing "{"', self._current.span)
    self._advance()

    members = []
    while not self._is_eof():
      if self._current.type == Token.Type.END_CURLY_BRACKET:
        break

      result = self._parse_assignement()
      members.append(result)

    if self._current.type != Token.Type.END_CURLY_BRACKET:
      raise ParserException('missing "}', self._current.span)

    expr_span = merge_span(start_span, self._current.span)
    self._advance()

    return StructExpression(
      LabelExpression(label_token.label, label_token.span),
      members, expr_span)

  def _parse_type(self):
    assert (self._current.type == Token.Type.TYPE)
    self._advance()

    return self._parse_assignement()

  def parse(self):
    expressions = []
    errors = []

    while not self._is_eof():
      try:
        if self._current.type == Token.Type.TYPE:
          expressions.append(self._parse_type())

        elif self._current.type == Token.Type.STRUCT:
          expressions.append(self._parse_struct())

        else:
          errors.append(
            ('unknown start of expression', self._current.span))
          self._advance()

      except ParserException as e:
        errors.append((e.message, e.span))

    return (expressions, errors)
