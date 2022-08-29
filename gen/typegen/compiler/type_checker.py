
from dataclasses import dataclass
from typing import List, Mapping, Tuple, Union

from compiler.parser import AssignExpression, StructExpression, LabelExpression, ArrayTypeExpression, ParsedExpression


@dataclass
class CheckedTypeExpression:
  id: str


@dataclass
class CheckedArrayTypeExpression:
  inner_type: CheckedTypeExpression
  length_label: str
  include_length: bool


@dataclass
class CheckedAssignExpression:
  id: str
  type: Union[CheckedTypeExpression, CheckedArrayTypeExpression]


@dataclass
class CheckedStructExpression:
  id: str
  members: List[CheckedAssignExpression]


CheckedExpression = Union[CheckedStructExpression, CheckedAssignExpression]


class TypeChecker:
  @staticmethod
  def check(expressions: List[ParsedExpression]):
    global_types: Mapping[str, ParsedExpression] = {}
    errors: List[Tuple[str, Tuple[int, int]]] = []

    def check_type_exists(expr: LabelExpression):
      if expr.label in ['u8', 'u16', 'u24', 'u32', 'i16', 'i32', 'str']:
        return True

      for type_id in global_types:
        if type_id == expr.label:
          return True

      errors.append((f'unknown type "{expr.label}"', expr.span))
      return False

    def type_references_self(self_label: str, expr: LabelExpression):
      if self_label == expr.label:
        errors.append(
            ('type expressions can not reference themselves', member.type.span))
        return True
      return False

    def check_assign_valid(expr: AssignExpression):
      if isinstance(expr.type, LabelExpression):
        if type_references_self(expr.id.label, expr.type):
          return False

        return check_type_exists(expr.type)

      elif isinstance(expr.type, ArrayTypeExpression):
        if type_references_self(expr.id.label, expr.type.inner_type):
          return False

        return check_type_exists(expr.type.inner_type)

      assert False, f'Recieved unknown type: "{type(expr.type).__name__}"'

    def check_id_unique(expr: ParsedExpression,
                        world: Mapping[str, ParsedExpression]):
      if current_type := world.get(expr.id.label, None):
        errors.append(
          (f'type with name "{expr.id.label}" originally defined here', current_type.id.span))
        errors.append(
          (f'type with name "{expr.id.label}" already defined', expr.id.span))
        return False
      return True

    checked_expressions: List[
      Union[CheckedAssignExpression, CheckedStructExpression]] = []

    def checked_type_expression(
      expr: Union[LabelExpression, ArrayTypeExpression]):
      if isinstance(expr, ArrayTypeExpression):
        return CheckedArrayTypeExpression(
            CheckedTypeExpression(expr.inner_type.label), expr.length_label, expr.include_length)
      else:
        return CheckedTypeExpression(expr.label)

    def checked_assign_expression(expr: AssignExpression):
      return CheckedAssignExpression(
          expr.id.label, checked_type_expression(expr.type))

    for expr in expressions:
      if isinstance(expr, AssignExpression):
        check_assign_valid(expr)

        if check_id_unique(expr, global_types):
          global_types[expr.id.label] = expr

        checked_expressions.append(checked_assign_expression(expr))

      if isinstance(expr, StructExpression):
        is_unique = check_id_unique(expr, global_types)

        local_types: Mapping[str, ParsedExpression] = {}
        checked_members: List[CheckedAssignExpression] = []
        for member in expr.members:
          check_id_unique(member, local_types)
          local_types[member.id.label] = member

          check_assign_valid(member)

          checked_members.append(checked_assign_expression(member))

        if is_unique:
          global_types[expr.id.label] = expr

        checked_expressions.append(CheckedStructExpression(
            expr.id.label, checked_members))

    return (checked_expressions, errors)
