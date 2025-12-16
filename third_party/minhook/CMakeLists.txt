cmake_minimum_required(VERSION 3.20)
project(MinHook LANGUAGES C)

# MinHook 源文件
set(MINHOOK_SOURCES
    src/buffer.c
    src/hook.c
    src/trampoline.c
    src/hde/hde32.c
    src/hde/hde64.c
)

add_library(minhook STATIC ${MINHOOK_SOURCES})

target_include_directories(minhook PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 禁用特定警告
if(MSVC)
    target_compile_options(minhook PRIVATE /W3)
endif()
