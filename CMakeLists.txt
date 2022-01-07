cmake_minimum_required(VERSION 3.5)
project(rsrcloader VERSION 0.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
	# Clang or AppleClang
	set(CMAKE_CXX_FLAGS "-Wall")
endif()

set(ABSL_PROPAGATE_CXX_STD ON)
add_subdirectory(third_party/abseil-cpp)
include_directories(third_party/abseil-cpp)

# Allow absolute include paths from //
include_directories(.)

add_subdirectory(bin)
add_subdirectory(core)
add_subdirectory(third_party/musashi)

add_library(rsrcfork resource_file.cc resource_group.cc resource.cc)
target_link_libraries(rsrcfork CORE_LIB)
target_link_libraries(rsrcfork absl::statusor)

add_executable(rsrcloader main.cc)
target_link_libraries(rsrcloader CORE_LIB MAIN_LIB MUSASHI_LIB rsrcfork)
target_link_libraries(rsrcloader absl::flags absl::statusor)
