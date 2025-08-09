include(../../gen/typegen/typegen.cmake)

add_library(DIALOG_LIB STATIC dialog_manager.cc)
target_link_libraries(DIALOG_LIB CORE_LIB FONT_TYPES MEMORY_LIB PICT_LIB RSRC_LIB SCREEN_LIB GRAFPORT_TYPES EVENT_TYPES)
