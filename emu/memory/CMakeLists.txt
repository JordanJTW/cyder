add_library(MEMORY_LIB OBJECT memory_manager.cc memory_map.cc)
target_link_libraries(MEMORY_LIB CORE_LIB GLOBAL_NAMES GENERATED_TYPES)
