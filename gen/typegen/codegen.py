import functools

from typing import List
from parser import Expression
from pathlib import Path


_READTYPE_PROTOTYPE = \
    "absl::StatusOr<{}> ReadType(const core::MemoryRegion& region, size_t ptr)"


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
      'u24': 3,
      'u32': 4,
      'i32': 4,
    }
    if size := builtin_type_sizes.get(type, None):
      return size

    for expr in reversed(self._type_expressions):
      if expr['label'] == type:
        return self._get_type_size(expr['type']['label'])

    # FIXME: Errors should be tagged with the span of the token
    self._errors.append(f'No size found for type: {type}')
    return 0

  def _get_c_type(self, type: dict):
    builtin_types = {
      'u8': 'uint8_t',
      'u16': 'uint16_t',
      'u24': 'uint24_t',
      'u32': 'uint32_t',
      'i16': 'int16_t',
      'i32': 'int32_t',
    }

    if type['variant'] == Expression.TypeVariant.ARRAY:
      (inner_type, is_struct) = self._get_c_type(type['inner_type'])
      return (f'std::vector<{inner_type}>', True)

    assert type['variant'] == Expression.TypeVariant.VALUE

    for struct in self._struct_expressions:
      if struct['label'] == type['label']:
        return (type['label'], True)

    if 'str' == type['label']:
      return ('std::string', True)

    return (builtin_types.get(type['label'], type['label']), False)

  def _write_type(self, file, label, type):
    file.write(f'using {label} = {type};\n')

  def _generate_type_decls(self, file):
    for expr in self._type_expressions:
      (type, is_struct) = self._get_c_type(expr['type'])
      self._write_type(file, expr['label'], type)

  def _write_struct(self, file, label, members):
    file.write(f'struct {label} {{\n')
    for member in members:
      (type, is_struct) = self._get_c_type(member['type'])
      file.write(f'  {type} {member["name"]};\n')
    file.write('\n  size_t size() const;\n')
    file.write('};\n')

  def _generate_struct_decls(self, file):
    for expr in self._struct_expressions:
      self._write_struct(file, expr['label'], expr['members'])

  def _write_read_type_declare(self, file, type):
    file.write(f'{_READTYPE_PROTOTYPE.format(type)};\n')

  def _generate_read_type_declare(self, file):
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

  def _get_first_field_type(self, type):
    assert type['variant'] == Expression.TypeVariant.VALUE, "loop type must be a struct or primitive"

    if type['label'] == 'str':
      return 'uint8_t'

    (c_type, is_struct) = self._get_c_type(type)
    if not is_struct:
      return c_type

    for struct in self._struct_expressions:
      if struct['label'] == c_type and len(struct['members']) > 0:
        return self._get_first_field_type(struct['members'][0]['type'])

    assert False, f'Type "{type}" not found'

  def _write_read_type(self, file, label, members):
    file.write(f'template<> {_READTYPE_PROTOTYPE.format(label)} {{\n')
    file.write(f'  {label} obj;\n')

    offset_variables = []
    local_variables = []
    offset = 0

    def generate_offset_str(prefix=""):
      if offset_variables:
        return f'{offset} + ' + ' + '.join(f'{prefix}{var}' for var in offset_variables)
      return str(offset)

    for member in members:
      (name, type_definition) = (member['name'], member['type'])
      offset_str = 'ptr + ' + generate_offset_str("obj.")
      if local_variables:
        offset_str = offset_str + ' + ' + ' + '.join(local_variables)

      if type_definition['variant'] == Expression.TypeVariant.VALUE:
        if type_definition['label'] == 'str':
          file.write(
            f'  obj.{name} = TRY(ReadType<std::string>(region, {offset_str}));\n')
          offset_variables.append(f'{name}.size() + 1')
        else:
          (c_type, is_struct) = self._get_c_type(type_definition)

          if is_struct:
            file.write(
                f'  obj.{name} = TRY(ReadType<{c_type}>(region, {offset_str}));\n')
            offset_variables.append(f'{name}.size()')
          else:
            type_size = self._get_type_size(type_definition['label'])
            # Check if |type_size| is word aligned (is a power of two):
            if type_size in [1, 2, 4, 8]:
              file.write(
                  f'  obj.{name} = betoh<{c_type}>(TRY(region.Copy<{c_type}>({offset_str})));\n')
            else:
              file.write(
                f'  obj.{name} = TRY(CopyWithWidth<{c_type}>(region, {offset_str}, {type_size}));\n')
            offset = offset + type_size
      elif type_definition['variant'] == Expression.TypeVariant.ARRAY:
        (inner_type_definition, length, variable, condition) = (
          type_definition['inner_type'], type_definition['length'], type_definition['variable'], type_definition['condition'])

        assert inner_type_definition['variant'] == Expression.TypeVariant.VALUE
        inner_type = inner_type_definition['label']

        (c_type, is_struct) = self._get_c_type(inner_type_definition)

        file.write(f'  size_t {name}_offset = 0;\n')

        if variable == Expression.Loop.VARIABLE:
          loop_sign = None
          if condition == Expression.LoopCondition.LESS_THAN:
            loop_sign = '<'
          elif condition == Expression.LoopCondition.LESS_THAN_OR_EQUAL_TO:
            loop_sign = '<='
          else:
            assert False, 'NOTREACHED'

          file.write(
            f'  for (size_t i = 0; i {loop_sign} obj.{length}; ++i) {{\n')
        elif variable == Expression.Loop.NULL_TERMINATED:
          condition_type = self._get_first_field_type(inner_type_definition)
          file.write(
            f'  while (TRY(region.Copy<{condition_type}>({offset_str} + {name}_offset)) != 0) {{\n')
        else:
          assert False, "NOTREACHED"

        if is_struct:
          file.write(
            f'    auto inner_obj = TRY(ReadType<{c_type}>(region, {offset_str} + {name}_offset));\n')
          file.write(
            f'    obj.{name}.push_back(inner_obj);\n')
          file.write(f'    {name}_offset += inner_obj.size();\n')
        else:
          file.write(
              f'    obj.{name}.push_back(betoh<{c_type}>(TRY(region.Copy<{c_type}>({offset_str} + {name}_offset))));\n')
          file.write(f'    {name}_offset += sizeof({c_type});\n')
        local_variables.append(f'{name}_offset')
        file.write('  }\n')

    file.write('  return obj;\n')
    file.write('}\n\n')

    file.write(f'size_t {label}::size() const {{\n')
    size_variables = []
    for member in members:
      (name, type_definition) = (member['name'], member['type'])

      if type_definition['variant'] == Expression.TypeVariant.ARRAY:
        (inner_type, length, variable) = (
          type_definition['inner_type'], type['length'], type['variable'])
        (c_type, is_struct) = self._get_c_type(inner_type)

        if is_struct:
          file.write(f'  size_t {name}_offset = 0;\n')
          file.write(f'  for (size_t i = 0; i < {name}.size(); ++i) {{\n')
          file.write(f'    {name}_offset += {name}[i].size();\n')
          file.write('  }\n')
          size_variables.append(f'{name}_offset')
        else:
          size_variables.append(
            f'({name}.size() * {self._get_type_size(inner_type["label"])})')

    offset_str = generate_offset_str()
    if size_variables:
      offset_str = offset_str + ' + ' + ' + '.join(size_variables)
    file.write(f'  return {offset_str};\n')
    file.write('}\n\n')

  def _generate_read_type(self, file):
    for expr in self._struct_expressions:
      self._write_read_type(file, expr['label'], expr['members'])

  def _write_struct_stream(self, file, label, members):
    file.write(
      f'std::ostream& operator<<(std::ostream& os, const {label}& obj) {{ return os')
    for index, member in enumerate(members):
      line_end = ' << ", "' if index + 1 != len(members) else ''
      stream_value = f'obj.{member["name"]}'
      if member['type']['variant'] == Expression.TypeVariant.ARRAY:
        type = member['type']
        inner_type = type['inner_type']['label']
        stream_value = f'"[{inner_type};" << obj.{member["name"]}.size() << "]"'
      # Cast value to an int to work around printing u8 and having them appear as char...
      # Without this any u8 set to 0 ends up being interpreted as an \0 for a string.
      elif member['type']['label'] == 'u8':
        stream_value = f'int({stream_value})'
      elif member['type']['label'] == 'str':
        stream_value = f'\"\\"\" << {stream_value} << \"\\"\"'

      file.write(f' << "{member["name"]}: " << {stream_value}{line_end}')
    file.write('; }\n')

  def _generate_struct_stream(self, file):
    for expr in self._struct_expressions:
      self._write_struct_stream(file, expr['label'], expr['members'])

  def _generate_source(self, output_path):
    with open(f'{output_path}.cc', 'w') as source:
      source.write(f'#include "{Path(output_path).name}.h"\n')
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
