include(../../gen/typegen/typegen.cmake)

typegen(CONTROL_TYPES control_manager.tdef)
target_link_libraries(CONTROL_TYPES WINDOW_TYPES)

add_library(control_manager STATIC control_manager.cc)
target_link_libraries(control_manager CORE_LIB CONTROL_TYPES MEMORY_LIB WINDOW_TYPES)
