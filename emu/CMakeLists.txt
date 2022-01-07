add_executable(emu main.cc)
target_link_libraries(emu CORE_LIB MAIN_LIB MUSASHI_LIB rsrcfork)
target_link_libraries(emu absl::flags absl::statusor)
