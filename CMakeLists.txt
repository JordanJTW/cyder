cmake_minimum_required(VERSION 3.5)
project(
  rsrcloader
  VERSION 0.0.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Disable warning for non-portable multichars used for resources
set(CMAKE_CXX_FLAGS "-Wall -Wno-multichar")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/exe)

# Fetch the latest gtest (main) and enable testing for the project
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG main)
FetchContent_MakeAvailable(googletest)
enable_testing()

set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(third_party/abseil-cpp)
include_directories(third_party/abseil-cpp)

# Allow absolute include paths from //
include_directories(.)

add_subdirectory(bin)
add_subdirectory(core)
add_subdirectory(emu)
add_subdirectory(gen)
add_subdirectory(third_party/musashi)
add_subdirectory(third_party/imgui)

include(gen/typegen/typegen.cmake)

typegen(RESOURCE_TYPES resource_types.tdef)

add_library(rsrcfork resource_file.cc resource_group.cc resource.cc)
target_link_libraries(rsrcfork CORE_LIB RESOURCE_TYPES TYPEGEN_PRELUDE)
target_link_libraries(rsrcfork absl::statusor)

add_executable(rsrcloader main.cc)
target_link_libraries(rsrcloader CORE_LIB MAIN_LIB MUSASHI_LIB rsrcfork TYPEGEN_PRELUDE)
target_link_libraries(rsrcloader absl::flags absl::statusor)