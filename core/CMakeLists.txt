include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

set(CORE_SRC logging_internal.cc memory_region.cc)

add_library(CORE_LIB ${CORE_SRC})

target_link_libraries(CORE_LIB absl::base absl::strings)