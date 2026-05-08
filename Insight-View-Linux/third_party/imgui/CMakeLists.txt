cmake_minimum_required(VERSION 3.10)
project(imgui)

# ImGui 只需要 C++11 或更高版本（你的项目已用 C++17，没问题）
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 核心源文件 + 你的后端（SDL2 + OpenGL3）
add_library(imgui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    backends/imgui_impl_sdl2.cpp
    backends/imgui_impl_opengl3.cpp
)

# 让外部项目能包含 imgui.h 和后端头文件
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

# 链接 SDL2 和 OpenGL（使用你项目 find_package 找到的目标）
target_link_libraries(imgui PRIVATE SDL2::SDL2 OpenGL::GL)