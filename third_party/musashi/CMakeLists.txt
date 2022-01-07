include_directories("${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_BINARY_DIR}")

add_executable(m68kmake src/m68kmake.c)

add_custom_command(
    OUTPUT  m68kops.c
    COMMAND m68kmake
              ${CMAKE_CURRENT_BINARY_DIR}
              ${CMAKE_CURRENT_SOURCE_DIR}/src/m68k_in.c
    DEPENDS m68kmake src/m68k_in.c
)

add_library(MUSASHI_LIB src/m68kcpu.c src/m68kdasm.c m68kops.c src/softfloat/softfloat.c)
target_compile_definitions(MUSASHI_LIB PRIVATE -DMUSASHI_CNF="${CMAKE_CURRENT_SOURCE_DIR}/m68kconf.h")