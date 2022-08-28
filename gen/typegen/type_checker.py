
from dataclasses import dataclass
from typing import List, Mapping, Tuple, Union

from parser import AssignExpression, StructExpression, TypeExpression, ArrayTypeExpression


@dataclass
class CheckedTypeExpression:
  id: str
  span: Tuple[int, int]


@dataclass
class CheckedArrayTypeExpression:
  inner_type: CheckedTypeExpression
  length_label: str
  include_length: bool
  span: Tuple[int, int]


@dataclass
class CheckedAssignExpression:
  id: str
  # FIXME: Fold this into `id` as a type?
  id_span: Tuple[int, int]
  type: Union[CheckedTypeExpression, CheckedArrayTypeExpression]
  span: Tuple[int, int]


@dataclass
class CheckedStructExpression:
  id: str
  # FIXME: Fold this into `id` as a type?
  id_span: Tuple[int, int]
  members: List[CheckedAssignExpression]
  span: Tuple[int, int]


class TypeChecker:
  @staticmethod
  def check(expressions: List[Union[AssignExpression, StructExpression]]):
    global_types: Mapping[str, Union[AssignExpression, StructExpression]] = {}
    errors: List[Tuple[str, Tuple[int, int]]] = []

    def check_type_exists(expr: TypeExpression):
      if expr.id in ['u8', 'u16', 'u24', 'u32', 'i16', 'i32', 'str']:
        return True

      for type_id in global_types:
        if type_id == expr.id:
          return True

      errors.append((f'unknown type "{expr.id}"', expr.span))
      return False

    def type_references_self(self_label: str, expr: TypeExpression):
      if self_label == expr.id:
        errors.append(
            ('type expressions can not reference themselves', member.type.span))
        return True
      return False

    def check_assign_valid(expr: AssignExpression):
      if isinstance(expr.type, TypeExpression):
        if type_references_self(expr.id, expr.type):
          return False

        return check_type_exists(expr.type)

      elif isinstance(expr.type, ArrayTypeExpression):
        if type_references_self(expr.id, expr.type.inner_type):
          return False

        return check_type_exists(expr.type.inner_type)

      assert False, f'Recieved unknown type: "{type(expr.type).__name__}"'

    def check_id_unique(
            expr: Union[AssignExpression, StructExpression], world):
      if expr.id in world.keys():
        errors.append(
          (f'type with name "{expr.id}" originally defined here', world[expr.id].id_span))
        errors.append(
          (f'type with name "{expr.id}" already defined', expr.id_span))
        return False
      return True

    checked_expressions = []

    def checked_type_expression(
      expr: Union[TypeExpression, ArrayTypeExpression]):
      if isinstance(expr, ArrayTypeExpression):
        return CheckedArrayTypeExpression(
            CheckedTypeExpression(expr.inner_type.id, expr.inner_type.span), expr.length_label, expr.include_length, expr.span)
      else:
        return CheckedTypeExpression(expr.id, expr.span)

    def checked_assign_expression(expr: AssignExpression):
      return CheckedAssignExpression(
          expr.id, expr.id_span, checked_type_expression(expr.type), expr.span)

    for expr in expressions:
      if isinstance(expr, AssignExpression):
        check_assign_valid(expr)

        if check_id_unique(expr, global_types):
          global_types[expr.id] = expr

        checked_expressions.append(checked_assign_expression(expr))

      if isinstance(expr, StructExpression):
        is_unique = check_id_unique(expr, global_types)

        local_types = {}
        checked_members = []
        for member in expr.members:
          check_id_unique(member, local_types)
          local_types[member.id] = member

          check_assign_valid(member)

          checked_members.append(checked_assign_expression(member))

        if is_unique:
          global_types[expr.id] = expr

        checked_expressions.append(CheckedStructExpression(
            expr.id, expr.id_span, checked_members, expr.span))

    return (checked_expressions, errors)
