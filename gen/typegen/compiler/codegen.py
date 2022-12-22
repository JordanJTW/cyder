import functools
import textwrap

from compiler.type_checker import CheckedTypeExpression, CheckedStructExpression, CheckedAssignExpression
from pathlib import Path
from typing import List, Tuple, Union

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
    self._errors: List[str] = []

  def _get_c_type(self, type: CheckedTypeExpression) -> str:
    builtin_types = {
      'u8': 'uint8_t',
      'u16': 'uint16_t',
      'u24': 'uint24_t',
      'u32': 'uint32_t',
      'i16': 'int16_t',
      'i32': 'int32_t',
    }

    for struct in self._struct_expressions:
      if struct.id == type.id:
        return type.id

    if 'str' == type.id:
      return 'std::string'

    return builtin_types.get(type.id, type.id)

  def _write_type(self, file, label, type):
    file.write(f'using {label} = {type};\n')

  def _generate_type_decls(self, file):
    for expr in self._type_expressions:
      c_type = self._get_c_type(expr.type)
      self._write_type(file, expr.id, c_type)

  def _write_struct(self, file, expr: CheckedStructExpression):
    file.write(f'struct {expr.id} {{\n')
    for member in expr.members:
      c_type = self._get_c_type(member.type)
      init_expr = '' if member.type.is_struct else '{0}'
      file.write(f'  {c_type} {member.id}{init_expr};\n')

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

  def _generate_header(self, output_path):
    with open(f'{output_path}.h', 'w') as header:
      header.write('#pragma once\n\n')
      self._write_includes(header, _HEADER_INCLUDES)
      header.write('\n')

      self._generate_type_decls(header)
      header.write('\n')

      self._generate_struct_decls(header)
      header.write('\n')

      self._generate_read_write_type_decls(header)
      header.write('\n')

      self._generate_struct_stream_decls(header)


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
          c_type = self._get_c_type(member.type)

          if member.type.is_struct:
            file.write(
                f'  obj.{member.id} = TRY(ReadType<{c_type}>(region, {offset_str}));\n')
            offset_variables.append(f'{member.id}.size()')
          elif member.type.id == 'u24':
            file.write(
              f'  obj.{member.id} = TRY(CopyU24(region, {offset_str}));\n')
            offset = offset + 3
          else:
            file.write(
                f'  obj.{member.id} = TRY(region.Read<{c_type}>({offset_str}));\n')
            offset = offset + member.type.size

    file.write('  return obj;\n')
    file.write('}\n\n')

    file.write(f'size_t {label}::size() const {{\n')
    offset_str = generate_offset_str()
    file.write(f'  return {offset_str};\n')
    file.write('}\n\n')

  def _write_write_type_value(self, file, type_expr: CheckedTypeExpression, name, indent):
    c_type = self._get_c_type(type_expr)

    if type_expr.is_struct:
      # If the struct is a 'str' we must account for the proceeding length byte
      is_string = type_expr.id == 'str'
      write(file, f"""
        RETURN_IF_ERROR(WriteType<{c_type}>({name}, region, total_offset));
        total_offset += {('1 + ' if is_string else '') + f'{name}.size()' };
      """, indent=indent)
    elif type_expr.id == 'u24':
      write(file, f"""
        RETURN_IF_ERROR(WriteU24({name}, region, total_offset));
        total_offset += 3;
      """, indent=indent)
    else:
      write(file, f"""
        RETURN_IF_ERROR(region.Write<{c_type}>(total_offset, {name}));
        total_offset += sizeof({c_type});
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

  def _generate_read_write_type(self, file):
    for expr in self._struct_expressions:
      self._write_read_type(file, expr.id, expr.members)
      self._write_write_type(file, expr.id, expr.members)

  def _write_struct_stream(self, file, label, members):
    file.write(
      f'std::ostream& operator<<(std::ostream& os, const {label}& obj) {{ return os << "{{ "')
    for index, member in enumerate(members):
      line_end = ' << ", "' if index + 1 != len(members) else ''
      stream_value = f'obj.{member.id}'

      # Cast value to an int to work around printing u8 and having them appear as char...
      # Without this any u8 set to 0 ends up being interpreted as an \0 for a string.
      if member.type.id == 'u8':
        stream_value = f'int({stream_value})'
      elif member.type.id == 'Boolean':
        stream_value = f'({stream_value} ? "True" : "False")'
      elif 'Handle' in member.type.id or member.type.id == 'Ptr':
        stream_value = f'std::hex << "0x" << {stream_value} << std::dec'
      elif member.type.id == 'str':
        stream_value = f'\"\\"\" << {stream_value} << \"\\"\"'

      file.write(f' << "{member.id}: " << {stream_value}{line_end}')
    file.write('<< " }"; }\n')

  def _generate_struct_stream(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream(file, expr.id, expr.members)

  def _generate_source(self, output_path):
    with open(f'{output_path}.cc', 'w') as source:
      source.write(f'#include "{Path(output_path).name}.h"\n')
      source.write('\n')

      self._write_includes(source, _SOURCE_INCLUDES)
      source.write('\n')

      self._generate_read_write_type(source)
      source.write('\n')

      self._generate_struct_stream(source)

  def generate(self, output_path):
    self._generate_header(output_path)
    self._generate_source(output_path)
    return self._errors
