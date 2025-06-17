# Copyright (c) 2022, Jordan Werthman
# SPDX-License-Identifier: BSD-2-Clause

find_package(Python3 3.8 COMPONENTS Interpreter)

# All Python in `typegen` should be assumed to be the generator:
file(GLOB_RECURSE TYPEGEN_SRC ${CMAKE_SOURCE_DIR}/gen/typegen/*.py)

# ! typegen: generates types based on a defintion file
macro(typegen target_name type_definition)
  get_filename_component(output_name ${type_definition} NAME)

  add_custom_command(
    OUTPUT ${output_name}.cc ${output_name}.h
    COMMAND
      ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/gen/typegen/main.py
      -i ${CMAKE_CURRENT_SOURCE_DIR}/${type_definition}
      -o ${CMAKE_CURRENT_BINARY_DIR}/${output_name}
      ${CMAKE_SOURCE_DIR}
    DEPENDS ${TYPEGEN_SRC} ${type_definition})

  add_library(${target_name} STATIC ${output_name}.cc)
  target_link_libraries(${target_name} CORE_LIB absl::statusor MEMORY_LOGGER_LIB TYPEGEN_PRELUDE)
  target_include_directories(${target_name} PUBLIC ${CMAKE_BINARY_DIR})
endmacro()
