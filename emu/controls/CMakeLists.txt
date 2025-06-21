include(../../gen/typegen/typegen.cmake)

typegen(CONTROL_TYPES control_manager.tdef)

add_library(control_manager STATIC control_manager.cc)
target_link_libraries(control_manager CORE_LIB CONTROL_TYPES WINDOW_TYPES)
