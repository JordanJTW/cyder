import functools
import textwrap

from typing import List, Tuple, Union
from parser import TypeExpression, ArrayTypeExpression, StructExpression, AssignExpression
from pathlib import Path


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
    self._type_expressions: List[AssignExpression] = [
        expr for expr in expressions
        if isinstance(expr, AssignExpression)
    ]
    self._struct_expressions: List[StructExpression] = [
        expr for expr in expressions
        if isinstance(expr, StructExpression)
    ]
    self._errors: List[str] = []

  @functools.lru_cache
  def _get_type_size(self, type_id: str):
    builtin_type_sizes = {
      'u8': 1,
      'u16': 2,
      'i16': 2,
      'u24': 3,
      'u32': 4,
      'i32': 4,
    }
    if size := builtin_type_sizes.get(type_id, None):
      return size

    for expr in reversed(self._type_expressions):
      if expr.id == type_id:
        return self._get_type_size(expr.type.id)

    # FIXME: Errors should be tagged with the span of the token
    self._errors.append(f'No size found for type: {type_id}')
    return 0

  def _get_c_type(self, type: Union[ArrayTypeExpression, TypeExpression]) -> Tuple[str, bool]:
    builtin_types = {
      'u8': 'uint8_t',
      'u16': 'uint16_t',
      'u24': 'uint24_t',
      'u32': 'uint32_t',
      'i16': 'int16_t',
      'i32': 'int32_t',
    }

    if isinstance(type, ArrayTypeExpression):
      (c_type, _) = self._get_c_type(type.inner_type)
      return (f'std::vector<{c_type}>', True)

    assert isinstance(type, TypeExpression)

    for struct in self._struct_expressions:
      if struct.id == type.id:
        return (type.id, True)

    if 'str' == type.id:
      return ('std::string', True)

    return (builtin_types.get(type.id, type.id), False)

  def _write_type(self, file, label, type):
    file.write(f'using {label} = {type};\n')

  def _generate_type_decls(self, file):
    for expr in self._type_expressions:
      (c_type, _) = self._get_c_type(expr.type)
      self._write_type(file, expr.id, c_type)

  def _write_struct(self, file, label, members: List[AssignExpression]):
    file.write(f'struct {label} {{\n')
    for member in members:
      (c_type, _) = self._get_c_type(member.type)
      file.write(f'  {c_type} {member.id};\n')
    write(file, """

        size_t size() const;
      };\n
    """)

  def _generate_struct_decls(self, file):
    for expr in self._struct_expressions:
      self._write_struct(file, expr.id, expr.members)

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

  def _get_first_field_type(self, type):
    assert isinstance(type, TypeExpression) or isinstance(
      type, StructExpression), "loop type must be a struct or primitive"

    if type.id == 'str':
      return 'uint8_t'

    (c_type, is_struct) = self._get_c_type(type)
    if not is_struct:
      return c_type

    for struct in self._struct_expressions:
      if struct.id == c_type and len(struct.members) > 0:
        return self._get_first_field_type(struct.members[0].type)

    assert False, f'Type "{type}" not found'

  def _write_read_type(self, file, label, members: List[AssignExpression]):
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

      if isinstance(member.type, TypeExpression):
        if member.type.id == 'str':
          file.write(
            f'  obj.{member.id} = TRY(ReadType<std::string>(region, {offset_str}));\n')
          offset_variables.append(f'{member.id}.size() + 1')
        else:
          (c_type, is_struct) = self._get_c_type(member.type)

          if is_struct:
            file.write(
                f'  obj.{member.id} = TRY(ReadType<{c_type}>(region, {offset_str}));\n')
            offset_variables.append(f'{member.id}.size()')
          elif member.type.id == 'u24':
            file.write(
              f'  obj.{member.id} = TRY(CopyU24(region, {offset_str}));\n')
            offset = offset + 3
          else:
            file.write(
                f'  obj.{member.id} = betoh<{c_type}>(TRY(region.Copy<{c_type}>({offset_str})));\n')
            offset = offset + self._get_type_size(member.type.id)

      elif isinstance(member.type, ArrayTypeExpression):
        assert isinstance(member.type.inner_type, TypeExpression)
        inner_type_expr: TypeExpression = member.type.inner_type

        (c_type, is_struct) = self._get_c_type(inner_type_expr)

        if member.type.length_label == 'null':
          condition_type = self._get_first_field_type(inner_type_expr)
          write(file, f"""
            size_t {member.id}_offset = 0;
            while (TRY(region.Copy<{condition_type}>({offset_str} + {member.id}_offset)) != 0) {{
          """, indent=2)
        else:
          write(file, f"""
            size_t {member.id}_offset = 0;
            for (size_t i = 0; i {'<=' if member.type.include_length else '<'} obj.{member.type.length_label}; ++i) {{
          """, indent=2)

        if is_struct:
          write(file, f"""
            auto inner_obj = TRY(ReadType<{c_type}>(region, {offset_str} + {member.id}_offset));
            obj.{member.id}.push_back(inner_obj);
            {member.id}_offset += inner_obj.size();
          }}
        """, indent=2)
        else:
          write(file, f"""
            obj.{member.id}.push_back(betoh<{c_type}>(TRY(region.Copy<{c_type}>({offset_str} + {member.id}_offset))));
            {member.id}_offset += sizeof({c_type});
          }}
        """, indent=2)
        local_variables.append(f'{member.id}_offset')

    file.write('  return obj;\n')
    file.write('}\n\n')

    file.write(f'size_t {label}::size() const {{\n')
    size_variables = []
    for member in members:
      if isinstance(member.type, ArrayTypeExpression):
        (c_type, is_struct) = self._get_c_type(member.type.inner_type)

        if is_struct:
          write(file, f"""
            size_t {member.id}_offset = 0;
            for (size_t i = 0; i < {member.id}.size(); ++i) {{
              {member.id}_offset += {member.id}[i].size();
            }}
          """, indent=2)
          size_variables.append(f'{member.id}_offset')
        else:
          size_variables.append(
            f'({member.id}.size() * {self._get_type_size(member.type.inner_type.id)})')

    offset_str = generate_offset_str()
    if size_variables:
      offset_str = offset_str + ' + ' + ' + '.join(size_variables)
    file.write(f'  return {offset_str};\n')
    file.write('}\n\n')

  def _write_write_type_value(self, file, type_expr: TypeExpression, name, indent):
    (c_type, is_struct) = self._get_c_type(type_expr)

    if is_struct:
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
        RETURN_IF_ERROR(region.Write<{c_type}>(total_offset, htobe({name})));
        total_offset += sizeof({c_type});
      """, indent=indent)

  def _write_write_type(self, file, label, members: List[AssignExpression]):
    write(file, f"""
      {_WRITETYPE_PROTOTYPE.format(label)} {{
        size_t total_offset = offset;
    """)

    for member in members:
      if isinstance(member.type, TypeExpression):
        self._write_write_type_value(
          file, member.type, f'obj.{member.id}', indent=2)
      elif isinstance(member.type, ArrayTypeExpression):
        write(file, f'for (const auto& item : obj.{member.id}) {{\n', indent=2)
        self._write_write_type_value(
          file, member.type.inner_type, 'item', indent=4)
        write(file, '}\n', indent=2)
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
      f'std::ostream& operator<<(std::ostream& os, const {label}& obj) {{ return os')
    for index, member in enumerate(members):
      line_end = ' << ", "' if index + 1 != len(members) else ''
      stream_value = f'obj.{member.id}'
      if isinstance(member.type, ArrayTypeExpression):
        stream_value = f'"[{member.type.inner_type};" << obj.{member.id}.size() << "]"'
      # Cast value to an int to work around printing u8 and having them appear as char...
      # Without this any u8 set to 0 ends up being interpreted as an \0 for a string.
      elif member.type.id == 'u8':
        stream_value = f'int({stream_value})'
      elif member.type.id == 'str':
        stream_value = f'\"\\"\" << {stream_value} << \"\\"\"'

      file.write(f' << "{member.id}: " << {stream_value}{line_end}')
    file.write('; }\n')

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
