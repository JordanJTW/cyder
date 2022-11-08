include(../../gen/typegen/typegen.cmake)

typegen(GRAFPORT_TYPES grafport_types.tdef)

add_library(SCREEN_LIB OBJECT bitmap_screen.cc)
target_link_libraries(SCREEN_LIB CORE_LIB GRAFPORT_TYPES)

add_library(PICT_LIB OBJECT pict_v1.cc)
target_link_libraries(PICT_LIB CORE_LIB GRAFPORT_TYPES)

add_library(GRAPHICS_LIB OBJECT quickdraw.cc)
target_link_libraries(GRAPHICS_LIB CORE_LIB GRAFPORT_TYPES MEMORY_LIB)
