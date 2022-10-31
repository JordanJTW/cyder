add_executable(colorswatch colorswatch.cc)
target_link_libraries(colorswatch CORE_LIB)
target_link_libraries(colorswatch absl::str_format)

add_executable(disasm disasm.cc)
target_link_libraries(disasm CORE_LIB MAIN_LIB TRAP_NAMES)
target_link_libraries(disasm absl::statusor absl::strings rsrcfork MUSASHI_LIB TYPEGEN_PRELUDE)

add_executable(updateinfo updateinfo.cc)
target_link_libraries(updateinfo CORE_LIB)

add_executable(macbinary macbinary.cc finder_flags.cc)
target_link_libraries(macbinary CORE_LIB MAIN_LIB rsrcfork)

add_executable(viewpict viewpict.cc)
target_link_libraries(viewpict CORE_LIB MAIN_LIB PICT_LIB GRAFPORT_TYPES TYPEGEN_PRELUDE)