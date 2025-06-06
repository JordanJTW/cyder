# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

from dataclasses import dataclass
from typing import List, Mapping, Tuple, Union

from compiler.type_parser import AssignExpression, StructExpression, LabelExpression, ParsedExpression, ParserException, TrapExpression, EnumExpression


@dataclass
class CheckedTypeExpression:
  id: str
  size: int
  base_type_id: str
  is_dynamic: bool = False
  is_struct: bool = False
  is_enum: bool = False
  is_ref_type: bool = False
  has_user_size: bool = False


@dataclass
class CheckedAssignExpression:
  id: str
  type: CheckedTypeExpression


@dataclass
class CheckedStructExpression:
  id: str
  members: List[CheckedAssignExpression]
  size: int
  is_dynamic: bool


@dataclass
class CheckedTrapExpression:
  id: str
  arguments: List[CheckedAssignExpression]
  ret: CheckedTypeExpression


CheckedExpression = Union[CheckedStructExpression, CheckedAssignExpression]


_STATIC_TYPE_SIZES = {'u8': 1, 'u16': 2, 'u24': 3, 'bool': 1,
                      'u32': 4, 'i16': 2, 'i32': 4, 'OSType': 4}


class TypeChecker:
  @staticmethod
  def check(external_expressions: List[CheckedExpression], expressions: List[ParsedExpression]):
    global_id_spans: Mapping[str, Tuple[int, int]] = {}
    global_types: Mapping[str, CheckedExpression] = {}
    errors: List[Tuple[str, Tuple[int, int]]] = []

    def check_type_exists(expr: LabelExpression):
      if size := _STATIC_TYPE_SIZES.get(expr.label, None):
        return CheckedTypeExpression(
            expr.label, size, base_type_id=expr.label)

      if expr.label == 'Ptr' or expr.label == 'Handle':
        return CheckedTypeExpression(
            expr.label, size=4, base_type_id=expr.label, is_ref_type=True)

      if expr.label == 'str':
        return CheckedTypeExpression(
            expr.label, size=1, base_type_id='str', is_dynamic=True, is_struct=True)

      ref = global_types.get(expr.label, None)
      if not ref:
        for ext in external_expressions:
          if ext.id == expr.label:
            ref = ext
            break

      if ref:
        if isinstance(ref, CheckedAssignExpression):
          return CheckedTypeExpression(
              expr.label, ref.type.size, ref.type.base_type_id, ref.type.is_dynamic, ref.type.is_struct)
        elif isinstance(ref, CheckedStructExpression):
          return CheckedTypeExpression(
              expr.label, ref.size, expr.label, ref.is_dynamic, is_struct=True)
        elif isinstance(ref, EnumExpression):
          return CheckedTypeExpression(
              expr.label, 4, expr.label, is_enum=True)

      raise ParserException(f'unknown type "{expr.label}"', expr.span)

    def check_assign_valid(expr: AssignExpression):
      # FIXME: With-in structs a member might have the same name as a
      #        global type id which will cause a false trigger here:
      if expr.id.label == expr.type.label:
        raise ParserException(
            'type expressions can not reference themselves', expr.type.span)

      checked_type = check_type_exists(expr.type)

      # Handle special case of user provided size (i.e. u8[64] or str[64])
      if expr.type.size != None:
        checked_type.size = expr.type.size
        checked_type.has_user_size = True

      return checked_type

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
            (f'type with name "{id.label}" already defined', id.span))
        errors.append(
            (f'type with name "{id.label}" originally defined here', current_span))
        return False
      return True

    for expr in expressions:
      if isinstance(expr, AssignExpression):
        checked_assign = check_assign_expr(expr)

        if check_id_unique(expr.id, global_id_spans):
          global_id_spans[expr.id.label] = expr.id.span

          if checked_assign:
            global_types[expr.id.label] = checked_assign

      elif isinstance(expr, StructExpression):
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

        if is_unique:
          global_id_spans[expr.id.label] = expr.id.span
          global_types[expr.id.label] = CheckedStructExpression(
              expr.id.label, checked_members, fixed_size, is_dynamic)

      elif isinstance(expr, TrapExpression):
        local_id_spans: Mapping[str, Tuple[int, int]] = {}
        checked_args: List[CheckedAssignExpression] = []

        for arg in expr.arguments:
          check_id_unique(arg.id, local_id_spans)
          local_id_spans[arg.id.label] = arg.id.span

          if checked_assign := check_assign_expr(arg):
            checked_args.append(checked_assign)

        checked_return = None
        if expr.ret:
          checked_return = check_type_exists(expr.ret)

        global_types[expr.name.label] = CheckedTrapExpression(
            expr.name.label, checked_args, checked_return)
        
      elif isinstance(expr, EnumExpression):
        if not check_id_unique(expr.id, global_id_spans):
          continue

        local_id_spans: Mapping[str, Tuple[int, int]] = {}
        for value in expr.values:
          check_id_unique(value.id, local_id_spans)
          local_id_spans[value.id.label] = value.id.span

          #TODO: Ensure enum values are also unique

        global_id_spans[expr.id.label] = expr.id.span
        global_types[expr.id.label] = expr

      else:
        errors.append(
            (f'unknown expression {expr.__class__.__name__}', expr.span))

    return (global_types.values(), errors)
