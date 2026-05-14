cmake_minimum_required(VERSION 3.20)
project(uvc_viewer VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VIEWER_ENABLE_MOCK "Enable mock sources" ON)

find_package(OpenGL REQUIRED)
find_package(SDL2 REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
find_library(INSIGHT9_LIB insight9)

# pkg_check_modules(HIDAPI REQUIRED hidapi-libusb)  # 或 hidapi-hidraw
pkg_check_modules(HIDAPI REQUIRED hidapi-hidraw)
pkg_check_modules(TurboJPEG REQUIRED libturbojpeg)

link_directories(${CMAKE_SOURCE_DIR})

add_subdirectory(third_party/imgui)

add_executable(Insight_Viewer
    src/main.cpp
    src/app/Application.cpp
    src/app/Config.cpp
    src/app/UVCScanner.cpp
    src/video/MockVideoSource.cpp
    src/video/UvcExtensionUnit.cpp
    src/video/V4l2VideoSource.cpp
    src/sensor/MockHidSensor.cpp
    src/sensor/HidapiSensor.cpp
    third_party/imgui/backends/imgui_impl_opengl2.cpp
)

# add_library(imgui STATIC
#     imgui.cpp
#     imgui_draw.cpp
#     imgui_tables.cpp
#     imgui_widgets.cpp
#     backends/imgui_impl_sdl2.cpp
#     backends/imgui_impl_opengl3.cpp
# )
# target_include_directories(imgui PUBLIC .)
# target_link_libraries(imgui PRIVATE SDL2::SDL2 OpenGL::GL)

target_include_directories(Insight_Viewer PRIVATE
    include
    ${HIDAPI_INCLUDE_DIRS}
    ${TurboJPEG_INCLUDE_DIRS}
    third_party
)

target_link_libraries(Insight_Viewer PRIVATE
    SDL2::SDL2
    OpenGL::GL
    fmt::fmt
    spdlog::spdlog
    # nlohmann_json::nlohmann_json
    imgui
    ${HIDAPI_LIBRARIES}
    ${TurboJPEG_LIBRARIES}
    Threads::Threads
    insight9
)

target_link_options(Insight_Viewer PRIVATE -Wl,-rpath,'$ORIGIN/..')
