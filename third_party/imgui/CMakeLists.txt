cmake_minimum_required(VERSION 3.20)
project(ImGui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

# ImGui 核心源文件
set(IMGUI_SOURCES
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    # 后端
    backends/imgui_impl_win32.cpp
    backends/imgui_impl_dx11.cpp
)

add_library(imgui STATIC ${IMGUI_SOURCES})

target_include_directories(imgui PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/backends
)

# 链接 DirectX 库
target_link_libraries(imgui PUBLIC
    d3d11
    dxgi
)
