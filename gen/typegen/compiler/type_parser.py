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


@dataclass
class TrapExpression:
  name: LabelExpression
  arguments: List[AssignExpression]
  ret: Optional[LabelExpression]
  span: Tuple[int, int]


@dataclass
class EnumValueExpression:
  id: LabelExpression
  value: int
  span: Tuple[int, int]


@dataclass
class EnumExpression:
  id: LabelExpression
  values: List[EnumValueExpression]
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

  def _peek(self):
    return self._tokens[self._index + 1]

  @property
  def _current(self) -> Token:
    return self._tokens[self._index]

  # Parses: <identifier>(<[> <digits> <]>);
  def _parse_type_expression(self):
    self._expect_next_token(Token.Type.IDENTIFIER, 'missing type')
    start_span = self._current.span

    label_expr = self._current

    maybe_size = None
    if self._peek().type == Token.Type.START_SQUARE_BRACKET:
      if label_expr.label not in _ALLOW_SIZE_POSTFIX:
        raise ParserException('[<size>] only allowed for types: [ ' +
                              ', '.join(_ALLOW_SIZE_POSTFIX) + ' ]', self._current.span)

      self._advance()
      size_token = self._expect_next_token(Token.Type.NUMBER, 'missing size')

      maybe_size = size_token.number

      self._expect_next_token(Token.Type.END_SQUARE_BRACKET, 'missing "]"')

    expr_span = merge_span(start_span, self._current.span)
    return LabelExpression(label_expr.label, expr_span, maybe_size)

  def _parse_assignement(self):
    start_span = self._current.span
    if self._current.type != Token.Type.IDENTIFIER:
      raise ParserException('missing label', self._current.span)

    label_token = self._current

    self._expect_next_token(Token.Type.COLON, 'missing ":"')

    type_expr = self._parse_type_expression()
    expr_span = merge_span(start_span, type_expr.span)

    return AssignExpression(
        LabelExpression(label_token.label, label_token.span),
        type_expr, expr_span)

  def _parse_struct(self):
    start_span = self._current.span

    assert (self._current.type == Token.Type.STRUCT)

    label_token = self._expect_next_token(
        Token.Type.IDENTIFIER, 'missing struct label')

    self._expect_next_token(Token.Type.START_CURLY_BRACKET, 'missing "{"')

    self._advance()

    members = []
    while not self._is_eof():
      if self._current.type == Token.Type.END_CURLY_BRACKET:
        break

      result = self._parse_assignement()
      self._expect_next_token(Token.Type.SEMICOLON, 'missing ";')
      self._advance()

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

    assignment = self._parse_assignement()

    self._expect_next_token(Token.Type.SEMICOLON, 'missing ";')
    self._advance()

    return assignment

  def _parse_macro(self, includes):
    assert (self._current.type == Token.Type.AT)

    token = self._expect_next_token(Token.Type.IDENTIFIER, 'missing macro')

    if token.label == 'include':
      self._expect_next_token(Token.Type.START_PARENTHESIS, 'missing "("')

      include_token = self._expect_next_token(
          Token.Type.STRING, 'expected string path to include')
      includes.append(include_token.label)

      self._expect_next_token(Token.Type.END_PARENTHESIS, 'missing ")"')
      self._advance()

  def _parse_trap(self):
    start_span = self._current.span
    assert self._current.type == Token.Type.TRAP

    trap_name = self._expect_next_token(
        Token.Type.IDENTIFIER, 'missing trap name')
    self._expect_next_token(Token.Type.START_PARENTHESIS, 'missing "("')
    self._advance()

    arguments = []
    while not self._is_eof():
      if self._current.type == Token.Type.END_PARENTHESIS:
        self._advance()
        break

      arguments.append(self._parse_assignement())
      self._advance()

      if self._current.type != Token.Type.COMMA and self._current.type != Token.Type.END_PARENTHESIS:
        raise ParserException('missing ","', self._current.span)

      if self._current.type == Token.Type.COMMA:
        self._advance()

    if self._current.type == Token.Type.SEMICOLON:
      expr_span = merge_span(start_span, self._current.span)
      self._advance()

      return TrapExpression(
          name=LabelExpression(label=trap_name.label, span=trap_name.span),
          arguments=arguments,
          ret=None,
          span=expr_span)

    elif self._current.type == Token.Type.COLON:
      ret_label = self._parse_type_expression()
      self._expect_next_token(Token.Type.SEMICOLON, 'missing ";"')
      expr_span = merge_span(start_span, self._current.span)
      self._advance()

      return TrapExpression(
          name=LabelExpression(label=trap_name.label, span=trap_name.span),
          arguments=arguments,
          ret=ret_label,
          span=expr_span)

    else:
      raise ParserException('missing return value', span=self._current.span)

  def _parse_enum(self):
    assert self._current.type == Token.Type.ENUM

    enum_start_span = self._current.span

    enum_id = self._expect_next_token(
        Token.Type.IDENTIFIER, 'missing enum <id> {')
    self._expect_next_token(Token.Type.START_CURLY_BRACKET, 'missing "{"')

    enum_values = []
    while not self._is_eof():
      if self._peek().type == Token.Type.END_CURLY_BRACKET:
        break

      name = self._expect_next_token(Token.Type.IDENTIFIER, 'missing name')
      start_span = self._current.span
      self._expect_next_token(Token.Type.COLON, 'missing ":"')
      value = self._expect_next_token(Token.Type.NUMBER, 'missing value')
      self._expect_next_token(Token.Type.SEMICOLON, 'missing ";"')
      end_span = self._current.span

      enum_values.append(EnumValueExpression(
          LabelExpression(name.label, name.span),
          value.number,
          merge_span(start_span, end_span)))

    self._expect_next_token(Token.Type.END_CURLY_BRACKET, 'missing "}"')
    merged_span = merge_span(enum_start_span, self._current.span)
    self._advance()

    return EnumExpression(
        LabelExpression(enum_id.label, span=enum_id.span),
        enum_values,
        merged_span)

  def _expect_next_token(self, token_type, error_message):
    self._advance()
    if self._current.type != token_type:
      raise ParserException(error_message, self._current.span)
    return self._current

  def parse(self):
    expressions = []
    includes = []
    errors = []

    while not self._is_eof():
      try:
        if self._current.type == Token.Type.TYPE:
          expressions.append(self._parse_type())

        elif self._current.type == Token.Type.STRUCT:
          expressions.append(self._parse_struct())

        elif self._current.type == Token.Type.TRAP:
          expressions.append(self._parse_trap())

        elif self._current.type == Token.Type.ENUM:
          expressions.append(self._parse_enum())

        elif self._current.type == Token.Type.AT:
          self._parse_macro(includes)

        else:
          errors.append(
              ('unknown start of expression', self._current.span))
          self._advance()

      except ParserException as e:
        errors.append((e.message, e.span))

    return (expressions, includes, errors)
