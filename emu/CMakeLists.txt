include(../gen/typegen/typegen.cmake)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_subdirectory(graphics)
add_subdirectory(memory)
add_subdirectory(trap)

typegen(SEGMENT_TYPES segment_types.tdef)

add_executable(emu main.cc event_manager.cc menu_manager.cc resource_manager.cc segment_loader.cc)
target_link_libraries(
  emu
  CORE_LIB
  IMGUI_LIB
  MAIN_LIB
  MEMORY_LIB
  MUSASHI_LIB
  PICT_LIB
  rsrcfork
  SCREEN_LIB
  GLOBAL_NAMES
  GRAPHICS_LIB
  TRAP_NAMES
  TRAP_LIB
  GENERATED_TYPES
  SEGMENT_TYPES
  TYPEGEN_PRELUDE)
target_link_libraries(emu absl::flags absl::statusor absl::strings GRAFPORT_TYPES)
target_link_libraries(emu ${SDL2_LIBRARIES})
