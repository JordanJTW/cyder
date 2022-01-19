find_package (Python3 COMPONENTS Interpreter)

add_custom_command(
    OUTPUT  trap_names.cc trap_names.h
    COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/trap_names_gen.py
        ${CMAKE_CURRENT_BINARY_DIR}/trap_names
    DEPENDS trap_names_gen.py trap_names.txt
)

add_library(TRAP_NAMES OBJECT trap_names.cc)
target_include_directories(TRAP_NAMES PUBLIC ${CMAKE_CURRENT_BINARY_DIR})