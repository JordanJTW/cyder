cmake_minimum_required(VERSION 3.18)
project(
  cyder
  VERSION 0.1.0
  LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
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

if(EMSCRIPTEN)
  string(APPEND CMAKE_CXX_FLAGS " -s USE_SDL=2")
  string(APPEND CMAKE_CXX_FLAGS " -s USE_PTHREADS=1")
  # Abseil triggers noisy warnings about deprecated builtins in Emscripten.
  string(APPEND CMAKE_CXX_FLAGS " -Wno-deprecated-builtins")

  string(APPEND CMAKE_EXE_LINKER_FLAGS " -s USE_SDL=2")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -s FORCE_FILESYSTEM=1")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -s PTHREAD_POOL_SIZE=1") # Thread used for emulator
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -s EXPORTED_RUNTIME_METHODS='[\"callMain\"]'")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -s MODULARIZE=1")
  string(APPEND CMAKE_EXE_LINKER_FLAGS " -s EXPORT_NAME=\"Cyder\"")
else() # Emscripten includes its own SDL2 but all other platforms need to find it
  find_package(SDL2 REQUIRED)
  include_directories(${SDL2_INCLUDE_DIRS})
endif()

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
