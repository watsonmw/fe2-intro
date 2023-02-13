cmake_minimum_required(VERSION 3.13)
project(fintro)

set(CMAKE_CXX_STANDARD 11)

include_directories(src)

# SDL based fintro
add_executable(
        fintro
        src/main-sdl.c
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
        src/modelcode.h
        src/modelcode.c
)

# Amiga fintro, this exe project is just to get IDE source code inspections - to actually build
# use 'amiga/build-ami-gcc.sh' (I build on Debian with bebbo's gcc).
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

# Define DEBUG c/c++ macro when compiling in debug mode
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -DDEBUG -DM_MEM_DEBUG -fno-strict-aliasing")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG -DM_MEM_DEBUG -fno-strict-aliasing")

IF(MINGW)
    include_directories(C:/msys64/mingw32/include)
    link_directories(C:/msys64/mingw32/lib)
    target_compile_definitions(fintro PRIVATE -Dmain=SDL_main)
    target_compile_options(fintro PRIVATE -mconsole)
ENDIF()

target_compile_definitions(fintro PRIVATE -DUSE_SDL -DFINTRO_SCREEN_RES=3 -DM_CLIB_USE_ALIGNED)
target_compile_options(fintro PRIVATE -ggdb)

# Point to your actual AmigaOS NDK
target_compile_definitions(fintro-amiga PRIVATE -DAMIGA -DFINTRO_SCREEN_RES=2)
target_include_directories(fintro-amiga PRIVATE C:/Amiga/m68k-amigaos/ndk-include C:/Amiga/m68k-amigaos/include)

IF(APPLE)
    find_library(SDL2_LIBRARY SDL2 REQUIRED)
    target_link_libraries(fintro ${COCOA_LIBRARY} ${SDL2_LIBRARY})
ENDIF()

IF(MINGW)
    target_link_libraries(fintro mingw32 SDL2main SDL2 opengl32 -static-libgcc)
ENDIF()

# Detect emscripten is used.
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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/fe2-intro.html
        ${CMAKE_CURRENT_BINARY_DIR}/fe2-intro.html
    )

    configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/src/manifest.json
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
