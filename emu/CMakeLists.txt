add_executable(emu main.cc memory_map.cc segment_loader.cc)
target_link_libraries(emu CORE_LIB MAIN_LIB MUSASHI_LIB rsrcfork disasm_trap)
target_link_libraries(emu absl::flags absl::statusor)

add_library(disasm_trap disasm_trap.cc)
target_link_libraries(disasm_trap absl::strings)