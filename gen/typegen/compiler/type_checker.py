
from dataclasses import dataclass
from typing import List, Mapping, Tuple, Union

from compiler.parser import AssignExpression, StructExpression, LabelExpression, ArrayTypeExpression, ParsedExpression, ParserException


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
    global_id_spans: Mapping[str, Tuple[int, int]] = {}
    global_types: Mapping[str, CheckedExpression] = {}
    errors: List[Tuple[str, Tuple[int, int]]] = []

    def check_type_exists(expr: LabelExpression):
      if expr.label in ['u8', 'u16', 'u24', 'u32', 'i16', 'i32', 'str']:
        return CheckedTypeExpression(expr.label)

      for type_id in global_types:
        if type_id == expr.label:
          return CheckedTypeExpression(expr.label)

      raise ParserException(f'unknown type "{expr.label}"', expr.span)

    def check_assign_valid(expr: AssignExpression):
      if isinstance(expr.type, LabelExpression):
        # FIXME: With-in structs a member might have the same name as a
        #        global type id which will cause a false trigger here:
        if expr.id.label == expr.type.label:
          raise ParserException(
            'type expressions can not reference themselves', expr.type.span)

        return check_type_exists(expr.type)

      elif isinstance(expr.type, ArrayTypeExpression):
        if expr.id.label == expr.type.inner_type.label:
          raise ParserException(
            'type expressions can not reference themselves', expr.type.span)

        checked_type = check_type_exists(expr.type.inner_type)
        return CheckedArrayTypeExpression(
            checked_type, expr.type.length_label, expr.type.include_length)

      assert False, f'Recieved unknown type: "{type(expr.type).__name__}"'

    def check_assign_expr(expr: AssignExpression):
      try:
        checked_type = check_assign_valid(expr)
        return CheckedAssignExpression(expr.id.label, checked_type)
      except ParserException as e:
        errors.append((e.message, e.span))
        return None

    def check_id_unique(id: LabelExpression,
                        id_to_span: Mapping[str, Tuple[int, int]]):
      if current_span := id_to_span.get(id.label, None):
        errors.append(
          (f'type with name "{id.label}" originally defined here', current_span))
        errors.append(
          (f'type with name "{id.label}" already defined', current_span))
        return False
      return True

    for expr in expressions:
      if isinstance(expr, AssignExpression):
        checked_assign = check_assign_expr(expr)

        if check_id_unique(expr.id, global_id_spans):
          global_id_spans[expr.id.label] = expr.id.span

          if checked_assign:
            global_types[expr.id.label] = checked_assign

      if isinstance(expr, StructExpression):
        is_unique = check_id_unique(expr.id, global_id_spans)

        local_id_spans: Mapping[str, Tuple[int, int]] = {}
        checked_members: List[CheckedAssignExpression] = []

        for member in expr.members:
          check_id_unique(member.id, local_id_spans)
          local_id_spans[member.id.label] = member.id.span

          if checked_assign := check_assign_expr(member):
            checked_members.append(checked_assign)

        if is_unique:
          global_id_spans[expr.id.label] = expr.id.span
          global_types[expr.id.label] = CheckedStructExpression(
              expr.id.label, checked_members)

    return (global_types.values(), errors)
