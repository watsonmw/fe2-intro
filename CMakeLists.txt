cmake_minimum_required(VERSION 3.13)
project(fintro)

set(CMAKE_CXX_STANDARD 11)

include_directories(src)

add_executable(
        fintro
        src/main-fintro.c
        src/render.h
        src/render.c
        src/renderinternal.h
        src/mlib.c
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
        fintro-amiga
        src/main-amiga.c
        src/render.h
        src/render.c
        src/renderinternal.h
        src/mlib.c
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

IF(MINGW)
    include_directories(C:/msys64/mingw32/include)
    link_directories(C:/msys64/mingw32/lib)
    target_compile_definitions(fintro PRIVATE -Dmain=SDL_main)
    target_compile_options(fintro PRIVATE -mconsole)
ENDIF()

target_compile_definitions(fintro PRIVATE -DUSE_SDL)
target_compile_definitions(fintro-amiga PRIVATE -DAMIGA -DFINTRO_SCREEN_RES=2 -DSURFACE_HEIGHT=400)
target_include_directories(fintro-amiga PRIVATE ../../m68k-amigaos/ndk-include ../../m68k-amigaos/include)
target_compile_options(fintro PRIVATE -ggdb)

IF(APPLE)
    find_library(SDL2_LIBRARY SDL2 REQUIRED)
    target_link_libraries(fintro ${COCOA_LIBRARY} ${SDL2_LIBRARY})
ENDIF()

IF(MINGW)
    target_link_libraries(fintro mingw32 SDL2main SDL2 opengl32 -static-libgcc)
ENDIF()
