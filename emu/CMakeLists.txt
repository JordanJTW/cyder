include(../cmake/gtest.cmake)
include(../gen/typegen/typegen.cmake)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_subdirectory(controls)
add_subdirectory(debug)
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

add_library(emulator STATIC emulator.cc)
target_link_libraries(emulator DEBUG_LIB MEMORY_LIB MUSASHI_LIB TRAP_NAMES TRAP_LIB)
gtest(emulator_tests)
target_link_libraries(emulator_tests emulator MEMORY_LIB MUSASHI_LIB TRAP_NAMES TRAP_LIB)

set(APP_DEPS
  emulator
  absl::flags
  absl::statusor
  absl::strings
  event_manager
  control_manager
  CORE_LIB
  DEBUG_LIB
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
  WINDOW_TYPES
  GRAFPORT_TYPES
  ${SDL2_LIBRARIES}
)

set(APP_SRCS
  main.cc
  debug_logger.cc
  menu_manager.cc
  menu_popup.cc
  segment_loader.cc
  window_manager.cc
)

add_executable(emu ${APP_SRCS})
target_link_libraries(emu ${APP_DEPS})

set(CMAKE_MACOSX_BUNDLE TRUE) # Enable bundle creation

add_executable(cyder MACOSX_BUNDLE ${APP_SRCS})
target_link_libraries(cyder ${APP_DEPS})

gtest(event_manager_tests)
target_link_libraries(event_manager_tests event_manager)

set_target_properties(cyder PROPERTIES
  MACOSX_BUNDLE_GUI_IDENTIFIER "com.binary.one.cyder"
  MACOSX_BUNDLE_BUNDLE_NAME "Cyder"
  MACOSX_BUNDLE_BUNDLE_VERSION "0.1"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1"
)
