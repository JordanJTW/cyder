add_custom_command(
    OUTPUT  trap_names.cc trap_names.h
    COMMAND
        ${CMAKE_CURRENT_SOURCE_DIR}/trap_names_gen.py
        ${CMAKE_CURRENT_BINARY_DIR}/trap_names
    DEPENDS trap_names_gen.py
)

add_library(TRAP_NAMES trap_names.cc)