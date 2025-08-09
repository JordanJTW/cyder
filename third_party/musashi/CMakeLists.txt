include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")

# Directly calling GCC ensures that "m68kmake" is _always_ built for the HOST
# even if the build system is cross-compiling.
# TODO: Find a cleaner way to do this?
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/m68kops.c
    COMMAND gcc
    -o ${CMAKE_CURRENT_BINARY_DIR}/m68kmake
    ${CMAKE_CURRENT_SOURCE_DIR}/src/m68kmake.c
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/m68kmake
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src/m68k_in.c
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/m68k_in.c
)

add_library(MUSASHI_LIB STATIC src/m68kcpu.c src/m68kdasm.c m68kops.c src/softfloat/softfloat.c)
target_compile_definitions(MUSASHI_LIB PRIVATE -DMUSASHI_CNF="${CMAKE_CURRENT_SOURCE_DIR}/m68kconf.h")
target_compile_options(MUSASHI_LIB PRIVATE -pthread)
