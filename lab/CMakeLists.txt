cmake_minimum_required(VERSION 3.15)
project(fps_external)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# External FPS Monitor (no injection)
add_executable(fps_monitor WIN32
    src/main.cpp
    src/overlay_window.cpp
    src/overlay_window.h
)

target_link_libraries(fps_monitor PRIVATE
    dwmapi
    d2d1
    dwrite
    psapi
)

if(MSVC)
    target_compile_options(fps_monitor PRIVATE /W3)
    set_target_properties(fps_monitor PROPERTIES
        LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\""
    )
endif()

set_target_properties(fps_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
