include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

set(CORE_SRC logging_internal.cc memory_reader.cc memory_region.cc)

add_library(CORE_LIB OBJECT ${CORE_SRC})
target_link_libraries(CORE_LIB absl::base absl::strings absl::statusor)

add_library(MAIN_LIB OBJECT status_main.cc)
target_link_libraries(MAIN_LIB CORE_LIB absl::flags_parse absl::statusor)
