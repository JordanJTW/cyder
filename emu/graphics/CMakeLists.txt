include(../../gen/typegen/typegen.cmake)

typegen(GRAFPORT_TYPES grafport_types.tdef)

add_library(GRAPHICS_LIB OBJECT quickdraw.cc)
target_link_libraries(GRAPHICS_LIB CORE_LIB GENERATED_TYPES GRAFPORT_TYPES MUSASHI_LIB)
