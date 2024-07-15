#!/bin/bash
set -e
set -x
CLANG=clang
# OSX - XCode clang won't build WASM, you have to use the homebrew version (or otherwise install clang with wasm
# support)
CLANG=/opt/homebrew/opt/llvm/bin/clang

BUILD_DIR=build_wasm

DEFINES="-DM_MEM_DEBUG -DM_LOG_ALLOCATIONS -DWASM_DIRECT -DFINTRO_SCREEN_RES=3 -DM_CLIB_DISABLE"
INCLUDES="-Isrc -Isrc/platform/wasm"
SOURCES="src/render.c src/fmath.c src/audio.c src/fintro.c src/assets.c src/platform/wasm/mlib-wasm.c src/platform/wasm/main-wasm.c src/platform/basicalloc.c"
# OPT="-g"
OPT="-O3"

# --import-memory   - Memory supplied to WASM code from the JavaScript side
# --allow-undefined - Tell linker to allow undefined symbols, so WASM functions can be supplied by JavaScript later
LINKER_OPTS="-Wl,--no-entry -Wl,--error-limit=0 -Wl,--import-memory -Wl,--allow-undefined"

# -mbulk-memory     - Use WASM bulk memory opts for memset(), memmove(), memcpy(), etc
# -nostdlib         - There is no WASM stdlib
CC_OPTS="-nostdlib -mbulk-memory"

mkdir -p $BUILD_DIR
cp game $BUILD_DIR

$CLANG --target=wasm32 $OPT $DEFINES $CC_OPTS $LINKER_OPTS -o $BUILD_DIR/fe2-intro.wasm $INCLUDES $SOURCES

cp data/model-overrides-le.dat $BUILD_DIR/model-overrides-le.dat
cp src/platform/wasm/fe2-intro.html $BUILD_DIR/fe2-intro.html
cp src/platform/wasm/manifest.json $BUILD_DIR/manifest.json

# Dump human readable wasm
# wasm2wat $BUILD_DIR/fe2-intro.wasm > $BUILD_DIR/fe2-intro.wat
