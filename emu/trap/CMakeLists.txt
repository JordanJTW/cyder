find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_library(TRAP_LIB STATIC trap_manager.cc trap_dispatcher.cc)
target_link_libraries(
  TRAP_LIB
  control_manager
  CORE_LIB
  DIALOG_LIB
  event_manager
  GRAPHICS_LIB
  MEMORY_LIB
  MUSASHI_LIB
  PICT_LIB
  RSRC_LIB
  TRAP_NAMES
  TYPEGEN_PRELUDE)
target_link_libraries(TRAP_LIB absl::statusor)
target_link_libraries(TRAP_LIB ${SDL2_LIBRARIES})
