import functools

from typing import List
from parser import Expression


_READTYPE_PROTOTYPE = \
    "absl::StatusOr<{}> ReadType(const core::MemoryRegion& region, size_t ptr)"


_HEADER_INCLUDES = [
    '<cstdint>',
    '<ostream>',
    '"absl/status/statusor.h"',
    '"core/memory_region.h"',
]

_SOURCE_INCLUDES = [
    '"core/endian_helpers.h"',
    '"core/status_helpers.h"',
    '"emu/memory/memory_map.h"'
]


def _get_c_type(type):
  builtin_types = {
    'u8': 'uint8_t',
    'u16': 'uint16_t',
    'u32': 'uint32_t',
    'i16': 'int16_t',
    'i32': 'int32_t'
  }
  return builtin_types.get(type, type)


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
      self._write_type(file, expr['label'], _get_c_type(expr['type']))

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
      file.write(f'  {_get_c_type(member["type"])} {member["name"]};\n')
    file.write('};\n')

  def _generate_struct_decls(self, file):
    for expr in self._struct_expressions:
      self._write_struct(file, expr['label'], expr['members'])

  def _write_read_type_declare(self, file, type):
    file.write(f'{_READTYPE_PROTOTYPE.format(type)};\n')

  def _generate_read_type_declare(self, file):
    file.write('template <typename T>\n')
    self._write_read_type_declare(file, 'T')

    for expr in self._struct_expressions:
      file.write('template<> ')
      self._write_read_type_declare(file, expr['label'])

  def _write_struct_stream_declare(self, file, label):
    file.write(f'std::ostream& operator<<(std::ostream&, const {label}&);\n')

  def _generate_struct_stream_declare(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream_declare(file, expr['label'])

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

      self._generate_read_type_declare(header)
      header.write('\n')

      self._generate_struct_stream_declare(header)

  def _write_read_type(self, file, label, members):
    file.write(f'template<> {_READTYPE_PROTOTYPE.format(label)} {{\n')
    file.write(f'  {label} obj;\n')
    for member in members:
      (name, type) = (member['name'], member['type'])
      type = _get_c_type(type)

      file.write(
        f'  obj.{name} = betoh<{type}>(TRY(cyder::memory::kSystemMemory.Copy<{type}>(ptr + int({label}::Offsets::{name}))));\n')
    file.write('   return obj;\n')
    file.write('}\n')

  def _generate_read_type(self, file):
    for expr in self._struct_expressions:
      self._write_read_type(file, expr['label'], expr['members'])

  def _write_struct_stream(self, file, label, members):
    file.write(
      f'std::ostream& operator<<(std::ostream& os, const {label}& obj) {{ return os')
    for index, member in enumerate(members):
      line_end = ' << ", "' if index + 1 != len(members) else ''
      # Cast value to an int to work around printing u8 and having them appear as char...
      # Without this any u8 set to 0 ends up being interpreted as an \0 for a string.
      file.write(
        f' << "{member["name"]}: " << int(obj.{member["name"]}){line_end}')
    file.write('; }\n')

  def _generate_struct_stream(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream(file, expr['label'], expr['members'])

  def _generate_source(self, output_path):
    with open(f'{output_path}.cc', 'w') as source:
      # FIXME: Use output path to determine the include
      source.write('#include "generated_types.h"\n')
      source.write('\n')

      self._write_includes(source, _SOURCE_INCLUDES)
      source.write('\n')

      self._generate_read_type(source)
      source.write('\n')

      self._generate_struct_stream(source)

  def generate(self, output_path):
    self._generate_header(output_path)
    self._generate_source(output_path)
    return self._errors
