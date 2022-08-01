find_package (Python3 3.8 COMPONENTS Interpreter)

file(GLOB TYPEGEN_SRC *.py)

add_custom_command(
    OUTPUT  generated_types.cc generated_types.h
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/main.py
        ${CMAKE_CURRENT_SOURCE_DIR}/example.tdef
        ${CMAKE_CURRENT_BINARY_DIR}/generated_types
    DEPENDS ${TYPEGEN_SRC} example.tdef
)

add_library(GENERATED_TYPES OBJECT generated_types.cc polyfill.cc)
target_include_directories(GENERATED_TYPES PUBLIC ${CMAKE_CURRENT_BINARY_DIR})