find_package(Python3 3.8 COMPONENTS Interpreter)

# All Python in `typegen` should be assumed to be the generator:
file(GLOB_RECURSE TYPEGEN_SRC ${CMAKE_SOURCE_DIR}/gen/typegen/*.py)

# ! typegen: generates types based on a defintion file
macro(typegen target_name type_definition)
  get_filename_component(output_name ${type_definition} NAME_WE)

  add_custom_command(
    OUTPUT ${output_name}.cc ${output_name}.h
    COMMAND
    ${Python3_EXECUTABLE}
    ${CMAKE_SOURCE_DIR}/gen/typegen/main.py
    ${CMAKE_CURRENT_SOURCE_DIR}/${type_definition}
    ${CMAKE_CURRENT_BINARY_DIR}/${output_name}
    DEPENDS ${TYPEGEN_SRC} ${type_definition}
  )

  add_library(${target_name} OBJECT ${output_name}.cc)
  target_link_libraries(${target_name} CORE_LIB absl::statusor TYPEGEN_PRELUDE)
  target_include_directories(${target_name} PUBLIC ${CMAKE_BINARY_DIR})
endmacro()