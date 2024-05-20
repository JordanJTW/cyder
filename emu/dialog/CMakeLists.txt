include(../../gen/typegen/typegen.cmake)

add_library(DIALOG_LIB OBJECT dialog_manager.cc)
target_link_libraries(DIALOG_LIB CORE_LIB MEMORY_LIB RSRC_LIB SCREEN_LIB GRAFPORT_TYPES)
