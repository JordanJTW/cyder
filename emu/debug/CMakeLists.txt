include(../../cmake/gtest.cmake)

add_library(MEMORY_LOGGER_LIB STATIC debug_manager.cc)
target_link_libraries(MEMORY_LOGGER_LIB CORE_LIB)

add_library(DEBUG_LIB STATIC debugger.cc)
target_link_libraries(DEBUG_LIB CORE_LIB MEMORY_LIB EVENT_TYPES WINDOW_TYPES)

gtest(debug_manager_test)
target_link_libraries(debug_manager_test CORE_LIB DEBUG_LIB GRAFPORT_TYPES MEMORY_LIB)