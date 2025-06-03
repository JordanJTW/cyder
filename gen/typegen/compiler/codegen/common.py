
from compiler.type_checker import CheckedTypeExpression, CheckedAssignExpression

def get_c_type(type: CheckedTypeExpression) -> str:
  builtin_types = {
      'u8': 'uint8_t',
      'u16': 'uint16_t',
      'u24': 'uint24_t',
      'u32': 'uint32_t',
      'i16': 'int16_t',
      'i32': 'int32_t',
  }

  if type.id in builtin_types:
    return builtin_types[type.id]
  
  if type.base_type_id in builtin_types:
    return builtin_types[type.base_type_id]

  if 'str' == type.id:
    return 'std::string'

  return type.id

def get_stream_format(accessor_prefix: str, expr: CheckedAssignExpression) -> str:
    stream_value = f'{accessor_prefix}{expr.id}'

    if expr.type.base_type_id == 'u8':
      if expr.type.has_user_size:
        # Handle special case of a byte array by printing its size
        return f'"u8[{expr.type.size}]"'
      else:
        # Ensure `u8` is not printed as a char (prints invisible/random chars
        # or is interpreted as an \0 i.e. null-terminator).
        return f'int({stream_value})'
    elif expr.type.id == 'bool':
      return f'({stream_value} ? "True" : "False")'
    elif expr.type.base_type_id == 'OSType':
      return f'OSTypeName({stream_value})'
    elif 'Handle' in expr.type.id or 'Ptr' in expr.type.id:
      return f'std::hex << "0x" << {stream_value} << std::dec'
    elif expr.type.id == 'str':
      return f'\"\\"\" << {stream_value} << \"\\"\"'
    else:
      return stream_value
