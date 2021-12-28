cmake_minimum_required(VERSION 3.5)
project(rsrcloader VERSION 0.0.0 LANGUAGES CXX)

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

add_subdirectory(core)
add_executable(rsrcloader main.cc rsrcloader.cc)
target_link_libraries(rsrcloader CORE_LIB)
target_link_libraries(rsrcloader absl::base absl::statusor)

add_executable(updateinfo updateinfo.cc)
target_link_libraries(updateinfo CORE_LIB)
