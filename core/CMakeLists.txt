include_directories("${CMAKE_CURRENT_SOURCE_DIR}")

set(CORE_SRC logging_internal.cc)

add_library(CORE_LIB ${CORE_SRC})