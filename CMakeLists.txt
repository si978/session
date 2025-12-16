cmake_minimum_required(VERSION 3.20)
project(FPSOverlay LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Windows 特定设置
if(WIN32)
    add_definitions(-DUNICODE -D_UNICODE)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-DNOMINMAX)
endif()

# ============================================================
# 第三方库
# ============================================================

# MinHook
add_subdirectory(third_party/minhook)

# ImGui
add_subdirectory(third_party/imgui)

# ============================================================
# FPS Overlay DLL
# ============================================================

file(GLOB OVERLAY_DLL_SOURCES
    "src/*.cpp"
    "src/*.h"
)

add_library(fps_overlay SHARED ${OVERLAY_DLL_SOURCES})

target_include_directories(fps_overlay PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party/minhook/include
    ${CMAKE_SOURCE_DIR}/third_party/imgui
)

target_link_libraries(fps_overlay PRIVATE
    minhook
    imgui
    d3d11
    dxgi
)

# ============================================================
# Injector 注入器
# ============================================================

add_executable(injector src/injector/main.cpp)

target_compile_definitions(injector PRIVATE UNICODE _UNICODE)

# 请求管理员权限
if(MSVC)
    set_target_properties(injector PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()

# ============================================================
# Test App (DX11 测试程序)
# ============================================================

add_executable(test_app WIN32 src/test_app/main.cpp)

target_link_libraries(test_app PRIVATE d3d11)

# ============================================================
# Launcher (托盘后台监控程序)
# ============================================================

add_executable(launcher WIN32 
    src/launcher/main.cpp
    src/launcher/launcher.rc
)

target_link_libraries(launcher PRIVATE shell32)

if(MSVC)
    set_target_properties(launcher PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()
