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

# Amiga target (Just to get IDE source code inspections - to actually build use 'amiga/build-ami-gcc.sh')
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

# Native WASM target (also just for IDE support, use scripts/build_wasm.sh to build)
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

# Define DEBUG c/c++ macro when compiling in debug mode
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG -DM_MEM_DEBUG -DM_ASSERT -fno-strict-aliasing")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -DM_MEM_DEBUG -DM_ASSERT -fno-strict-aliasing")

target_compile_definitions(fintro PRIVATE -DM_USE_SDL -DM_USE_STDLIB -DFINTRO_SCREEN_RES=3)
target_compile_options(fintro PRIVATE -ggdb)

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
    target_link_libraries(fintro ${COCOA_LIBRARY} ${SDL2_LIBRARY})
ENDIF()

IF(MINGW)
    target_compile_definitions(fintro PRIVATE -Dmain=SDL_main)
    target_compile_options(fintro PRIVATE -mconsole)
    target_link_libraries(fintro mingw32 SDL2main SDL2 opengl32 -static-libgcc)
ENDIF()

# Enscripten + SDL (Old wasm target)
if(EMSCRIPTEN)
    string(APPEND CMAKE_C_FLAGS " -s DEMANGLE_SUPPORT=1")
    string(APPEND CMAKE_C_FLAGS " -s DISABLE_EXCEPTION_CATCHING=2")
    string(APPEND CMAKE_C_FLAGS " -s USE_SDL=2")
    string(APPEND CMAKE_C_FLAGS " -s USE_SDL_IMAGE=2")
    string(APPEND CMAKE_C_FLAGS " -s ENVIRONMENT=web")
    string(APPEND CMAKE_C_FLAGS " -s WASM=1")
    string(APPEND CMAKE_C_FLAGS " -s INITIAL_MEMORY=64mb")

    # Debug flags
    # string(APPEND CMAKE_C_FLAGS " -DDEBUG")
    # string(APPEND CMAKE_C_FLAGS " -gsource-map")
    # string(APPEND CMAKE_C_FLAGS " --source-map-base http://localhost:8000/build_emscripten/")

    # Release flags
    string(APPEND CMAKE_C_FLAGS " -O3")

    configure_file(
        src/platform/enscripten/fe2-intro.html
        ${CMAKE_CURRENT_BINARY_DIR}/fe2-intro.html
    )

    configure_file(
        src/platform/wasm/manifest.json
        ${CMAKE_CURRENT_BINARY_DIR}/manifest.json
    )

    # Allow the directory /data to be fetched by the web browser.
    file(GLOB data_files "./data/*")
    foreach(file ${data_files})
        file(RELATIVE_PATH relative_file ${CMAKE_SOURCE_DIR} ${file})
        string(APPEND CMAKE_C_FLAGS " --preload-file ${file}@/${relative_file}")
    endforeach()
    string(APPEND CMAKE_C_FLAGS " --preload-file ${CMAKE_SOURCE_DIR}/game@game")
endif()
