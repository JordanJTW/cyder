find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

add_library(IMGUI_LIB OBJECT src/imgui.cpp src/imgui_demo.cpp src/imgui_draw.cpp src/imgui_tables.cpp src/imgui_widgets.cpp src/backends/imgui_impl_sdl.cpp src/backends/imgui_impl_sdlrenderer.cpp)
target_include_directories(IMGUI_LIB PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/src/backends")