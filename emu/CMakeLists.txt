find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_executable(emu main.cc memory_manager.cc memory_map.cc resource_manager.cc segment_loader.cc stack_helpers.cc system_types.cc trap_manager.cc)
target_link_libraries(emu CORE_LIB IMGUI_LIB MAIN_LIB MUSASHI_LIB rsrcfork GLOBAL_NAMES TRAP_NAMES)
target_link_libraries(emu absl::flags absl::statusor)
target_link_libraries(emu ${SDL2_LIBRARIES})
