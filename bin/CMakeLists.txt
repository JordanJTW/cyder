add_executable(colorswatch colorswatch.cc)
target_link_libraries(colorswatch CORE_LIB)
target_link_libraries(colorswatch absl::str_format)

add_executable(disasm disasm.cc)
target_link_libraries(disasm CORE_LIB MAIN_LIB SEGMENT_TYPES TRAP_NAMES)
target_link_libraries(disasm absl::statusor absl::strings RSRC_LIB MUSASHI_LIB
  TYPEGEN_PRELUDE)

add_executable(macbinary macbinary.cc finder_flags.cc)
target_link_libraries(macbinary CORE_LIB MAIN_LIB MACBINARY_LIB RSRC_LIB)

add_executable(rsrcdump rsrcdump.cc)
target_link_libraries(rsrcdump CORE_LIB MAIN_LIB MUSASHI_LIB RSRC_LIB
  TYPEGEN_PRELUDE)
target_link_libraries(rsrcdump absl::flags absl::statusor)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_executable(screen screen.cc)
target_link_libraries(
  screen
  CORE_LIB
  MAIN_LIB
  PICT_LIB
  REGION_LIB
  SCREEN_LIB
  GRAFPORT_TYPES
  TYPEGEN_PRELUDE)
target_link_libraries(screen ${SDL2_LIBRARIES})

add_executable(viewpict viewpict.cc)
target_link_libraries(viewpict CORE_LIB MAIN_LIB PICT_LIB GRAFPORT_TYPES
  TYPEGEN_PRELUDE)

add_executable(asm2array asm2array.cc)
target_link_libraries(asm2array absl::status CORE_LIB MAIN_LIB MUSASHI_LIB)
