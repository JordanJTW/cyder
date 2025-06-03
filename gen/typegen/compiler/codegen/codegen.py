# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

import os
import textwrap

from compiler.type_checker import CheckedTypeExpression, CheckedStructExpression, CheckedAssignExpression, CheckedTrapExpression
from compiler.type_parser import EnumExpression
from compiler.codegen.common import get_c_type, get_stream_format
from pathlib import Path
from typing import List

_READTYPE_PROTOTYPE = \
    "template<> absl::StatusOr<{}> ReadType(const core::MemoryRegion& region, size_t offset)"

_WRITETYPE_PROTOTYPE = \
    "template<> absl::Status WriteType(const {}& obj, core::MemoryRegion& region, size_t offset)"

_HEADER_INCLUDES = [
    '<cstdint>',
    '<ostream>',
    '<string>',
    '"gen/typegen/typegen_prelude.h"',
    '"absl/status/statusor.h"',
    '"core/memory_region.h"',
]

_SOURCE_INCLUDES = [
    '"core/endian_helpers.h"',
    '"core/status_helpers.h"',
    '"emu/memory/memory_map.h"'
]


def write(file, contents: str, indent: int = 0):
  if contents.startswith('\n'):
    contents = contents[1:]
  file.write(textwrap.indent(
      textwrap.dedent(contents), ' ' * indent))


def snake_to_camel(snake_str):
  return ''.join(word.capitalize() for word in snake_str.split('_'))


class CodeGenerator:
  def __init__(self, expressions):
    self._type_expressions: List[CheckedAssignExpression] = [
        expr for expr in expressions
        if isinstance(expr, CheckedAssignExpression)
    ]
    self._struct_expressions: List[CheckedStructExpression] = [
        expr for expr in expressions
        if isinstance(expr, CheckedStructExpression)
    ]
    self._trap_expressions: List[CheckedTrapExpression] = [
        expr for expr in expressions
        if isinstance(expr, CheckedTrapExpression)
    ]
    self._enum_expressions: List[EnumExpression] = [
        expr for expr in expressions
        if isinstance(expr, EnumExpression)
    ]
    self._errors: List[str] = []



  def _write_type(self, file, label, type):
    file.write(f'using {label} = {type};\n')

  def _generate_type_decls(self, file):
    for expr in self._type_expressions:
      self._write_type(file, expr.id, get_c_type(expr.type))

  def _generate_enum_decls(self, file):
    for expr in self._enum_expressions:
      file.write(f'enum class {expr.id.label} {{\n')
      for value in expr.values:
        file.write(f'  {value.id.label} = {value.value},\n')
      file.write('};\n')

  def _write_struct(self, file, expr: CheckedStructExpression):
    file.write(f'struct {expr.id} {{\n')
    for member in expr.members:
      init_expr = '{0}'
      # Handle `field: u8[32]` as a C style array i.e. `uint8_t field[32]`
      if member.type.has_user_size and member.type.id == 'u8':
        init_expr = f'[{member.type.size}]'
      elif member.type.is_struct:
        init_expr = ''

      file.write(f'  {get_c_type(member.type)} {member.id}{init_expr};\n')

    if not expr.is_dynamic:
      write(
          file, f"""\nconst static size_t fixed_size = {expr.size};""", indent=2)

    write(file, """

        size_t size() const;
      };\n
    """)

  def _write_offsets(self, file, expr: CheckedStructExpression):
    if expr.is_dynamic:
      return

    field_offset = 0
    file.write(f'namespace {expr.id}Fields {{\n')
    for member in expr.members:
      write(file,
            f"""const static Field {member.id} = {{.offset = 0x{field_offset:x}, .size = {member.type.size}}};\n""", indent=2)
      field_offset += member.type.size
    file.write(f'}}  // namespace {expr.id}Fields\n\n')

  def _generate_struct_decls(self, file):
    for expr in self._struct_expressions:
      self._write_offsets(file, expr)
      self._write_struct(file, expr)

  def _write_read_write_type_decls(self, file, type):
    file.write(f'{_READTYPE_PROTOTYPE.format(type)};\n')
    file.write(f'{_WRITETYPE_PROTOTYPE.format(type)};\n')

  def _generate_read_write_type_decls(self, file):
    for expr in self._struct_expressions:
      self._write_read_write_type_decls(file, expr.id)

  def _write_struct_stream_decl(self, file, label):
    file.write(f'std::ostream& operator<<(std::ostream&, const {label}&);\n')

  def _generate_struct_stream_decls(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream_decl(file, expr.id)

  def _write_includes(self, file, includes):
    for include in includes:
      file.write(f'#include {include}\n')

  def _generate_trap_interface(self, file, output_path):
    file_name = os.path.splitext(os.path.basename(output_path))[0]
    class_name = snake_to_camel(file_name)

    file.write('namespace gen {\n')
    file.write(
        f'class {class_name} {{\n public:\n  virtual ~{class_name}() = default;\n')
    for trap in self._trap_expressions:
      argument_string = ', '.join(
          f'{get_c_type(a.type)} {a.id}' for a in trap.arguments)
      return_string = f'absl::StatusOr<{get_c_type(trap.ret)}>' if trap.ret else 'void'
      file.write(
          f'  virtual {return_string} {trap.id}({argument_string}) = 0;\n')
    file.write('};')
    file.write('}  // namespace gen')

  def _generate_header(self, include_paths, output_path):
    with open(f'{output_path}.h', 'w') as header:
      header.write('#pragma once\n\n')
      self._write_includes(header, _HEADER_INCLUDES)
      header.write('\n')

      for path in include_paths:
        header.write(f'#include "{path}"\n')
      header.write('\n')

      self._generate_type_decls(header)
      header.write('\n')

      self._generate_enum_decls(header)
      header.write('\n')

      self._generate_struct_decls(header)
      header.write('\n')

      self._generate_read_write_type_decls(header)
      header.write('\n')

      self._generate_struct_stream_decls(header)
      header.write('\n')

      self._generate_trap_interface(header, output_path)

  def _write_read_type(self, file, label, members: List[CheckedAssignExpression]):
    write(file, f"""
      {_READTYPE_PROTOTYPE.format(label)} {{
        struct {label} obj;
    """)

    offset_variables = []
    local_variables = []
    offset = 0

    def generate_offset_str(prefix=""):
      if offset_variables:
        return f'{offset} + ' + ' + '.join(f'{prefix}{var}' for var in offset_variables)
      return str(offset)

    for member in members:
      offset_str = 'offset + ' + generate_offset_str("obj.")
      if local_variables:
        offset_str = offset_str + ' + ' + ' + '.join(local_variables)

      if isinstance(member.type, CheckedTypeExpression):
        if member.type.id == 'str':
          file.write(
              f'  obj.{member.id} = TRY(ReadType<std::string>(region, {offset_str}));\n')
          offset_variables.append(f'{member.id}.size() + 1')
        else:
          if member.type.is_struct:
            file.write(
                f'  obj.{member.id} = TRY(ReadType<{get_c_type(member.type)}>(region, {offset_str}));\n')
            offset_variables.append(f'{member.id}.size()')
          elif member.type.id == 'u24':
            file.write(
                f'  obj.{member.id} = TRY(CopyU24(region, {offset_str}));\n')
            offset = offset + 3
          elif member.type.id == 'u8' and member.type.has_user_size:
            file.write(
                f'  RETURN_IF_ERROR(region.ReadRaw(obj.{member.id}, {offset_str}, {member.type.size}));\n')
            offset = offset + member.type.size
          else:
            file.write(
                f'  obj.{member.id} = TRY(region.Read<{get_c_type(member.type)}>({offset_str}));\n')
            offset = offset + member.type.size

    file.write('  return obj;\n')
    file.write('}\n\n')

    file.write(f'size_t {label}::size() const {{\n')
    offset_str = generate_offset_str()
    file.write(f'  return {offset_str};\n')
    file.write('}\n\n')

  def _write_write_type_value(self, file, type_expr: CheckedTypeExpression, name, indent):
    if type_expr.is_struct:
      # If the struct is a 'str' we must account for the proceeding length byte
      is_string = type_expr.id == 'str'
      write(file, f"""
        RETURN_IF_ERROR(WriteType<{get_c_type(type_expr)}>({name}, region, total_offset));
        total_offset += {('1 + ' if is_string else '') + f'{name}.size()'};
      """, indent=indent)
    elif type_expr.id == 'u24':
      write(file, f"""
        RETURN_IF_ERROR(WriteU24({name}, region, total_offset));
        total_offset += 3;
      """, indent=indent)
    elif type_expr.id == 'u8' and type_expr.has_user_size:
      write(file, f"""
        RETURN_IF_ERROR(region.WriteRaw({name}, total_offset, {type_expr.size}));
        total_offset += {type_expr.size};
      """, indent=indent)
    elif type_expr.id == 'bool':
      # sizeof(bool) is not guaranteed to be 1 according to the C++ standard.
      write(file, f"""
        RETURN_IF_ERROR(region.Write<bool>(total_offset, {name}));
        total_offset += 1;
      """, indent=indent)
    else:
      write(file, f"""
        RETURN_IF_ERROR(region.Write<{get_c_type(type_expr)}>(total_offset, {name}));
        total_offset += {type_expr.size};
      """, indent=indent)

  def _write_write_type(self, file, label, members: List[CheckedAssignExpression]):
    write(file, f"""
      {_WRITETYPE_PROTOTYPE.format(label)} {{
        size_t total_offset = offset;
    """)

    for member in members:
      self._write_write_type_value(
          file, member.type, f'obj.{member.id}', indent=2)
    write(file, """
        return absl::OkStatus();
      }\n
    """)

  def _write_struct_stream(self, file, label, members):
    file.write(
        f'std::ostream& operator<<(std::ostream& os, const {label}& obj) {{ return os << "{{ "')
    for index, member in enumerate(members):
      line_end = ' << ", "' if index + 1 != len(members) else ''
      stream_value = get_stream_format('obj.', member)

      file.write(f' << "{member.id}: " << {stream_value}{line_end}')
    file.write('<< " }"; }\n')

  def _generate_struct_stream(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream(file, expr.id, expr.members)

  def _generate_source(self, include_paths, output_path):
    with open(f'{output_path}.cc', 'w') as source:
      source.write(f'#include "{Path(output_path).name}.h"\n')
      source.write('\n')

      self._write_includes(source, _SOURCE_INCLUDES)
      source.write('\n')

      for path in include_paths:
        source.write(f'#include "{path}"\n')
      source.write('\n')

      for expr in self._struct_expressions:
        self._write_read_type(source, expr.id, expr.members)
        self._write_write_type(source, expr.id, expr.members)
      source.write('\n')

      self._generate_struct_stream(source)

  def generate(self, include_paths, output_path):
    generated_include_paths = [f'{path}.h' for path in include_paths]

    self._generate_header(generated_include_paths, output_path)
    self._generate_source(generated_include_paths, output_path)
    return self._errors
