find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_subdirectory(memory)
add_subdirectory(trap)

add_executable(emu main.cc resource_manager.cc segment_loader.cc)
target_link_libraries(
  emu
  CORE_LIB
  IMGUI_LIB
  MAIN_LIB
  MEMORY_LIB
  MUSASHI_LIB
  rsrcfork
  GLOBAL_NAMES
  TRAP_NAMES
  TRAP_LIB
  GENERATED_TYPES)
target_link_libraries(emu absl::flags absl::statusor absl::strings)
target_link_libraries(emu ${SDL2_LIBRARIES})
