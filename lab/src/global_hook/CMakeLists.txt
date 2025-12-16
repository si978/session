cmake_minimum_required(VERSION 3.15)
project(fps_global_hook)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MinHook path
set(MINHOOK_DIR "${CMAKE_SOURCE_DIR}/../../third_party/minhook")

# MinHook source files
set(MINHOOK_SOURCES
    ${MINHOOK_DIR}/src/buffer.c
    ${MINHOOK_DIR}/src/hook.c
    ${MINHOOK_DIR}/src/trampoline.c
    ${MINHOOK_DIR}/src/hde/hde32.c
    ${MINHOOK_DIR}/src/hde/hde64.c
)

# Hook DLL
add_library(fps_hook SHARED
    fps_hook.cpp
    ${MINHOOK_SOURCES}
)

target_include_directories(fps_hook PRIVATE
    ${MINHOOK_DIR}/include
)

target_link_libraries(fps_hook PRIVATE
    d3d11
    dxgi
)

# Monitor executable
add_executable(fps_monitor WIN32
    fps_monitor.cpp
)

target_link_libraries(fps_monitor PRIVATE
    shell32
)

# Output directory
set_target_properties(fps_hook fps_monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)
