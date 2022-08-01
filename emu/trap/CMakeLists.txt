find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_library(TRAP_LIB OBJECT trap_manager.cc)
target_link_libraries(TRAP_LIB CORE_LIB MEMORY_LIB MUSASHI_LIB rsrcfork
                      TRAP_NAMES)
target_link_libraries(TRAP_LIB absl::statusor)
target_link_libraries(TRAP_LIB ${SDL2_LIBRARIES})
