add_executable(emu main.cc memory_manager.cc memory_map.cc segment_loader.cc stack_helpers.cc)
target_link_libraries(emu CORE_LIB MAIN_LIB MUSASHI_LIB rsrcfork TRAP_NAMES)
target_link_libraries(emu absl::flags absl::statusor)
