find_package(Python3 3.8 COMPONENTS Interpreter)

add_subdirectory(typegen)

add_custom_command(
  OUTPUT trap_names.cc trap_names.h
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/trap_names_gen.py
          ${CMAKE_CURRENT_BINARY_DIR}/trap_names
  DEPENDS trap_names_gen.py trap_names.txt)

add_library(TRAP_NAMES STATIC trap_names.cc)
target_include_directories(TRAP_NAMES PUBLIC ${CMAKE_BINARY_DIR})

add_custom_command(
  OUTPUT global_names.cc global_names.h
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/global_names_gen.py
          ${CMAKE_CURRENT_BINARY_DIR}/global_names
  DEPENDS global_names_gen.py global_names.txt)

add_library(GLOBAL_NAMES STATIC global_names.cc)
target_include_directories(GLOBAL_NAMES PUBLIC ${CMAKE_BINARY_DIR})
