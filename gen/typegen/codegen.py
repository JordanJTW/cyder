import functools

from typing import List
from parser import Expression


class CodeGenerator:
  def __init__(self, expressions):
    self._type_expressions = [
        expr.data for expr in expressions
        if expr.type == Expression.Type.TYPE
    ]
    self._struct_expressions = [
        expr.data for expr in expressions
        if expr.type == Expression.Type.STRUCT
    ]
    self._errors: List[str] = []

  @functools.lru_cache
  def _get_type_size(self, type):
    builtin_type_sizes = {
      'u8': 1,
      'u16': 2,
      'i16': 2,
      'u32': 4,
      'i32': 4,
    }
    if size := builtin_type_sizes.get(type, None):
      return size

    for expr in reversed(self._type_expressions):
      if expr['label'] == type:
        return self._get_type_size(expr['type'])

    # FIXME: Errors should be tagged with the span of the token
    self._errors.append(f'No size found for type: {type}')
    return 0

  def _write_type(self, file, label, type):
    file.write(f'using {label} = {type};\n')

  def _generate_type_decls(self, file):
    for expr in self._type_expressions:
      def get_type():
        builtin_types = {
          'u8': 'uint8_t',
          'u16': 'uint16_t',
          'u32': 'uint32_t',
          'i16': 'int16_t',
          'i32': 'int32_t'
        }
        type = expr["type"]
        return builtin_types.get(type, type)

      self._write_type(file, expr['label'], get_type())

  def _write_type_offsets(self, file, members):
    file.write('  enum class Offsets {\n')
    offset = 0
    for member in members:
      (type, name) = (member['type'], member['name'])
      file.write(f'    {name} = {offset},\n')
      type_size = self._get_type_size(type)
      offset = offset + type_size
    file.write('  };\n')

  def _write_struct(self, file, label, members):
    file.write(f'struct {label} {{\n')
    self._write_type_offsets(file, members)
    for member in members:
      file.write(f'  {member["type"]} {member["name"]};\n')
    file.write('};\n')

  def _generate_struct_decls(self, file):
    for expr in self._struct_expressions:
      self._write_struct(file, expr['label'], expr['members'])

  def _write_struct_stream_declare(self, file, label):
    file.write(f'std::ostream& operator<<(std::ostream&, const {label}&);\n')

  def _generate_struct_stream_declare(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream_declare(file, expr['label'])

  def _generate_header(self, output_path):
    with open(f'{output_path}.h', 'w') as header:
      header.write('#include <cstdint>\n#include <ostream>\n')
      header.write('\n')

      self._generate_type_decls(header)
      header.write('\n')

      self._generate_struct_decls(header)
      header.write('\n')

      self._generate_struct_stream_declare(header)

  def _write_struct_stream(self, file, label, members):
    file.write(
      f'std::ostream& operator<<(std::ostream& os, const {label}& obj) {{ return os')
    for member in members:
      file.write(f'<< "{member["name"]}:" << obj.{member["name"]}')
    file.write('; }\n')

  def _generate_struct_stream(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream(file, expr['label'], expr['members'])

  def _generate_source(self, output_path):
    with open(f'{output_path}.cc', 'w') as source:
      source.write('#include "types.h"\n')
      source.write('\n')

      self._generate_struct_stream(source)

  def generate(self, output_path):
    self._generate_header(output_path)
    self._generate_source(output_path)
    return self._errors
