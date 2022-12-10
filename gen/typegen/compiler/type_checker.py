
from dataclasses import dataclass
from typing import List, Mapping, Tuple, Union

from compiler.parser import AssignExpression, StructExpression, LabelExpression, ArrayTypeExpression, ParsedExpression, ParserException


@dataclass
class CheckedTypeExpression:
  id: str
  size: int
  is_dynamic: bool
  is_struct: bool


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
  size: int
  is_dynamic: bool


CheckedExpression = Union[CheckedStructExpression, CheckedAssignExpression]


_INTEGER_SIZES = {'u8': 1, 'u16': 2, 'u24': 3, 'u32': 4, 'i16': 2, 'i32': 4}


class TypeChecker:
  @staticmethod
  def check(expressions: List[ParsedExpression]):
    global_id_spans: Mapping[str, Tuple[int, int]] = {}
    global_types: Mapping[str, CheckedExpression] = {}
    errors: List[Tuple[str, Tuple[int, int]]] = []

    def check_type_exists(expr: LabelExpression):
      if size := _INTEGER_SIZES.get(expr.label, None):
        return CheckedTypeExpression(
            expr.label, size, is_dynamic=False, is_struct=False)

      if expr.label == 'str':
        return CheckedTypeExpression(
            expr.label, size=1, is_dynamic=True, is_struct=False)

      if ref := global_types.get(expr.label, None):
        if isinstance(ref, CheckedAssignExpression):
          if isinstance(ref.type, CheckedTypeExpression):
            return CheckedTypeExpression(
                expr.label, ref.type.size, ref.type.is_dynamic, ref.type.is_struct)
          else:
            raise ParserException(
                f'arrays can not be global', global_id_spans[expr.label])
        elif isinstance(ref, CheckedStructExpression):
          return CheckedTypeExpression(
              expr.label, ref.size, ref.is_dynamic, True)

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

        fixed_size = 0
        is_dynamic = False
        for member in expr.members:
          check_id_unique(member.id, local_id_spans)
          local_id_spans[member.id.label] = member.id.span

          if checked_assign := check_assign_expr(member):
            checked_members.append(checked_assign)

            if isinstance(checked_assign.type, CheckedTypeExpression):
              if checked_assign.type.id == 'str':
                is_dynamic = True
              fixed_size += checked_assign.type.size
            
            if isinstance(checked_assign.type, CheckedArrayTypeExpression):
              is_dynamic = True

        if is_unique:
          global_id_spans[expr.id.label] = expr.id.span
          global_types[expr.id.label] = CheckedStructExpression(
              expr.id.label, checked_members, fixed_size, is_dynamic)

    return (global_types.values(), errors)
