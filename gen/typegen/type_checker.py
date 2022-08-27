
from typing import List, Mapping, Tuple, Union

from parser import AssignExpression, StructExpression, TypeExpression, ArrayTypeExpression


class TypeChecker:
  @staticmethod
  def check(expressions: List[Union[AssignExpression, StructExpression]]):
    global_types: Mapping[str, Union[AssignExpression, StructExpression]] = {}
    errors: List[Tuple[str, Tuple[int, int]]] = []

    def check_type_exists(expr: TypeExpression):
      if expr.id in ['u8', 'u16', 'u24', 'u32', 'i16', 'i32', 'str']:
        return

      for type_id in global_types:
        if type_id == expr.id:
          return

      errors.append((f'unknown type "{expr.id}"', expr.span))

    def check_assign_valid(expr: AssignExpression):
      if isinstance(expr.type, TypeExpression):
        check_type_exists(expr.type)

      elif isinstance(expr.type, ArrayTypeExpression):
        check_type_exists(expr.type.inner_type)

    def check_id_unique(
            expr: Union[AssignExpression, StructExpression], world):
      if expr.id in world.keys():
        errors.append(
          (f'type with name "{expr.id}" originally defined here', world[expr.id].id_span))
        errors.append(
          (f'type with name "{expr.id}" already defined', expr.id_span))
        return False
      return True

    for expr in expressions:
      if isinstance(expr, AssignExpression):
        if isinstance(expr.type, TypeExpression) and expr.id == expr.type.id:
          errors.append(
            ('type expressions can not reference themselves', expr.type.span))
          # This is already a known invalid reference so checking with
          # `check_assign_valid()` will produce a redundant error
        else:
          check_assign_valid(expr)

        if check_id_unique(expr, global_types):
          global_types[expr.id] = expr

      if isinstance(expr, StructExpression):
        is_unique = check_id_unique(expr, global_types)

        local_types = {}
        for member in expr.members:
          check_id_unique(member, local_types)
          local_types[member.id] = member

          check_assign_valid(member)

        if is_unique:
          global_types[expr.id] = expr

    return errors
