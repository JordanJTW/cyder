add_executable(colorswatch colorswatch.cc)
target_link_libraries(colorswatch CORE_LIB)
target_link_libraries(colorswatch absl::str_format)

add_executable(disasm disasm.cc)
target_link_libraries(disasm CORE_LIB MAIN_LIB TRAP_NAMES)
target_link_libraries(disasm absl::statusor absl::strings rsrcfork MUSASHI_LIB)

add_executable(updateinfo updateinfo.cc)
target_link_libraries(updateinfo CORE_LIB)
