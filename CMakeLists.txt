cmake_minimum_required(VERSION 3.13)
project(fintro)

set(CMAKE_C_STANDARD 11)

include_directories(src)

set(CMAKE_VERBOSE_MAKEFILE ON)

# SDL based fintro
add_executable(
        fintro
        src/platform/sdl/main-sdl.c
        src/platform/sdl/mlib-sdl.c
        src/render.h
        src/render.c
        src/renderinternal.h
        src/mlib.h
        src/fintro.h
        src/fintro.c
        src/audio.h
        src/audio.c
        src/assets.h
        src/assets.c
        src/fmath.h
        src/fmath.c
        src/modelcode.h
        src/modelcode.c
)

# Amiga target (Just to get IDE source code inspections - to actually build use 'amiga/build-amiga-gcc.sh')
add_executable(
        fintro-amiga
        src/platform/amiga/main-amiga.c
        src/platform/mlib-log-stdlib.c
        src/render.h
        src/render.c
        src/renderinternal.h
        src/mlib.h
        src/fintro.h
        src/fintro.c
        src/audio.h
        src/audio.c
        src/assets.h
        src/assets.c
        src/fmath.h
        src/fmath.c
)

# Native WASM target (also just for IDE support, use scripts/build-wasm.sh to build)
add_executable(
        fintro-wasm
        src/platform/basicalloc.c
        src/platform/basicalloc.h
        src/platform/wasm/main-wasm.c
        src/platform/wasm/mlib-wasm.c
        src/platform/wasm/sprintf.c
        src/render.h
        src/render.c
        src/renderinternal.h
        src/mlib.h
        src/fintro.h
        src/fintro.c
        src/audio.h
        src/audio.c
        src/assets.h
        src/assets.c
        src/fmath.h
        src/fmath.c
)

add_executable(
        fintro-imgui
        src/platform/imgui/gl3w/GL/gl3w.c
        src/platform/imgui/imgui/imgui_impl_opengl3.cpp
        src/platform/imgui/imgui/imgui_impl_sdl.cpp
        src/platform/imgui/imgui/imgui.cpp
        src/platform/imgui/imgui/imgui_draw.cpp
        src/platform/imgui/imgui/imgui_widgets.cpp
        src/platform/imgui/annotation.cpp
        src/platform/imgui/annotation.h
        src/platform/imgui/builddata.c
        src/platform/imgui/builddata.h
        src/platform/imgui/drawinfo.cpp
        src/platform/imgui/main-imgui.cpp
        src/platform/imgui/mcpplib.cpp
        src/platform/imgui/mcpplib.h
        src/platform/imgui/mini.c
        src/platform/imgui/mini.h
        src/platform/imgui/modelviewer.cpp
        src/platform/imgui/modelviewer.h
        src/platform/imgui/tinypng/TinyPngOut.cpp
        src/platform/sdl/mlib-sdl.c
        src/assets.c
        src/assets.h
        src/audio.c
        src/audio.h
        src/fintro.c
        src/fintro.h
        src/fmath.c
        src/fmath.h
        src/mlib.h
        src/modelcode.c
        src/modelcode.h
        src/render.c
        src/render.h
        src/renderinternal.h
)

# Define DEBUG c/c++ macro when compiling in debug mode
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG -DM_MEM_DEBUG -DM_ASSERT -fno-strict-aliasing")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -DM_MEM_DEBUG -DM_ASSERT -fno-strict-aliasing")

target_compile_definitions(fintro PRIVATE -DM_USE_SDL -DM_USE_STDLIB -DFINTRO_SCREEN_RES=3)
target_compile_options(fintro PRIVATE -ggdb)

# ImGui target
target_compile_definitions(fintro-imgui PRIVATE -DM_USE_SDL -DM_USE_STDLIB -DFINTRO_SCREEN_RES=3 -DFINTRO_INSPECTOR)
target_include_directories(fintro-imgui PRIVATE src/platform/imgui)
target_include_directories(fintro-imgui PRIVATE src/platform/imgui src/platform/imgui/imgui src/platform/imgui/gl3w)
target_compile_options(fintro-imgui PRIVATE -ggdb)
target_compile_features(fintro-imgui PRIVATE cxx_std_17)
target_compile_definitions(fintro-imgui PRIVATE -DUSE_SDL)

# Point to your actual AmigaOS NDK
target_compile_definitions(fintro-amiga PRIVATE -DAMIGA -DM_USE_STDLIB -DFINTRO_SCREEN_RES=2)
target_include_directories(fintro-amiga PRIVATE C:/Amiga/m68k-amigaos/ndk-include C:/Amiga/m68k-amigaos/include)

# Native WASM target
target_compile_definitions(fintro-wasm PRIVATE -DFINTRO_SCREEN_RES=3 -DWASM_DIRECT -DM_CLIB_DISABLE)
target_include_directories(fintro-wasm PRIVATE src/platform/wasm)
target_compile_options(fintro-wasm PRIVATE --target=wasm32)
target_link_options(fintro-wasm PRIVATE --target=wasm32)

IF(APPLE)
    find_library(SDL2_LIBRARY SDL2 REQUIRED)
    find_package(OpenGL REQUIRED)
    find_library(COCOA_LIBRARY Cocoa REQUIRED)
    target_link_libraries(fintro ${COCOA_LIBRARY} ${SDL2_LIBRARY})
    target_link_libraries(fintro-imgui ${COCOA_LIBRARY} ${SDL2_LIBRARY} ${OPENGL_LIBRARIES})
    target_compile_definitions(fintro-imgui PRIVATE -DIMGUI_IMPL_OPENGL_LOADER_GL3W)
ENDIF()

IF(MINGW)
    target_compile_definitions(fintro PRIVATE -Dmain=SDL_main)
    target_compile_options(fintro PRIVATE -mconsole)
    target_link_libraries(fintro mingw32 SDL2main SDL2 opengl32 -static-libgcc)

    target_compile_definitions(fintro-imgui PRIVATE -Dmain=SDL_main)
    target_compile_options(fintro-imgui PRIVATE -mconsole)
    target_link_libraries(fintro-imgui mingw32 SDL2main SDL2 opengl32 -static-libgcc -static-libstdc++)
ENDIF()
