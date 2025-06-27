include(../../cmake/gtest.cmake)

add_library(MEMORY_LIB STATIC memory_manager.cc memory_map.cc)
target_link_libraries(MEMORY_LIB CORE_LIB GLOBAL_NAMES GENERATED_TYPES)

gtest(memory_map_tests)
target_link_libraries(memory_map_tests CORE_LIB GLOBAL_NAMES GRAFPORT_TYPES
                      MEMORY_LIB TYPEGEN_PRELUDE)
