include(../../gen/typegen/typegen.cmake)

typegen(GRAFPORT_TYPES grafport_types.tdef)
target_link_libraries(GRAFPORT_TYPES BASE_TYPES)

add_library(SCREEN_LIB STATIC bitmap_image.cc)
target_link_libraries(SCREEN_LIB CORE_LIB GRAFPORT_TYPES)

add_library(PICT_LIB STATIC pict_v1.cc)
target_link_libraries(PICT_LIB CORE_LIB GRAFPORT_TYPES)

add_library(GRAPHICS_LIB STATIC quickdraw.cc)
target_link_libraries(GRAPHICS_LIB CORE_LIB GRAFPORT_TYPES MEMORY_LIB SCREEN_LIB)
