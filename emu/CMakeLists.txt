include(../cmake/gtest.cmake)
include(../gen/typegen/typegen.cmake)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_subdirectory(dialog)
add_subdirectory(graphics)
add_subdirectory(memory)
add_subdirectory(rsrc)
add_subdirectory(trap)

typegen(SEGMENT_TYPES segment_types.tdef)
typegen(EVENT_TYPES event_manager.tdef)
add_dependencies(EVENT_TYPES BASE_TYPES GRAFPORT_TYPES)
typegen(WINDOW_TYPES window_manager.tdef)
add_dependencies(WINDOW_TYPES BASE_TYPES GRAFPORT_TYPES)

add_library(event_manager event_manager.cc)
target_link_libraries(event_manager CORE_LIB EVENT_TYPES TYPEGEN_PRELUDE)

add_executable(
  emu
  main.cc
  debug_logger.cc
  menu_manager.cc
  menu_popup.cc
  segment_loader.cc
  window_manager.cc)
target_link_libraries(
  emu
  event_manager
  CORE_LIB
  MAIN_LIB
  MEMORY_LIB
  MUSASHI_LIB
  PICT_LIB
  DIALOG_LIB
  RSRC_LIB
  RESOURCE_MANAGER
  SCREEN_LIB
  GLOBAL_NAMES
  GRAPHICS_LIB
  TRAP_NAMES
  TRAP_LIB
  GENERATED_TYPES
  SEGMENT_TYPES
  TYPEGEN_PRELUDE
  WINDOW_TYPES)
target_link_libraries(emu absl::flags absl::statusor absl::strings
                      GRAFPORT_TYPES)
target_link_libraries(emu ${SDL2_LIBRARIES})

gtest(event_manager_tests)
target_link_libraries(event_manager_tests event_manager)
