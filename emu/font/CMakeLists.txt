include(../../gen/typegen/typegen.cmake)

typegen(FONT_TYPES font_types.tdef)

add_library(font STATIC basic_font.cc res_font.cc)
target_link_libraries(font CORE_LIB GRAFPORT_TYPES FONT_TYPES RESOURCE_MANAGER MEMORY_LIB)

