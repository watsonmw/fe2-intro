set -e
set -x
CLANG=clang-15
#CLANG=/usr/local/Cellar/llvm/17.0.6/bin/clang
BUILD_DIR=build_wasm

mkdir -p $BUILD_DIR
cp game $BUILD_DIR
# -mbulk-memory   : WASM memset / memcpy
# --import-memory : We'll supply memory to use from JavaScript side
#$CLANG --target=wasm32 -g -O1 -nostdlib -Wl,--no-entry -mbulk-memory -Wl,--error-limit=0 -Wl,--export=__heap_base -Wl,--export=__heap_end -Wl,--import-memory -o $BUILD_DIR/fe2-intro.wasm -Wl,--allow-undefined -Isrc -Isrc/platform/wasm src/render.c src/fmath.c src/audio.c src/fintro.c src/assets.c src/platform/wasm/mlib-wasm.c src/platform/wasm/main-wasm.c
# $CLANG --target=wasm32 -DM_MEM_DEBUG -DWASM_DIRECT -DAUDIO_BUFFERED_PLAYBACK_WASM -DFINTRO_SCREEN_RES=3 -g -nostdlib -Wl,--no-entry -mbulk-memory -Wl,--error-limit=0 -Wl,--import-memory -o $BUILD_DIR/fe2-intro.wasm -Wl,--allow-undefined -Isrc -Isrc/platform/wasm src/render.c src/fmath.c src/audio.c src/fintro.c src/assets.c src/platform/wasm/mlib-wasm.c src/platform/wasm/main-wasm.c
$CLANG --target=wasm32 -DM_MEM_DEBUG -DM_LOG_ALLOCATIONS -DWASM_DIRECT -DFINTRO_SCREEN_RES=3 -DM_CLIB_DISABLE -g -nostdlib -Wl,--no-entry -mbulk-memory -Wl,--error-limit=0 -Wl,--import-memory -o $BUILD_DIR/fe2-intro.wasm -Wl,--allow-undefined -Isrc -Isrc/platform/wasm src/render.c src/fmath.c src/audio.c src/fintro.c src/assets.c src/platform/wasm/mlib-wasm.c src/platform/wasm/main-wasm.c src/platform/basicalloc.c
#$CLANG --target=wasm32 -g -O1 -nostdlib -Wl,--no-entry -mbulk-memory -Wl,--error-limit=0 -Wl,--export=__heap_base -Wl,--export=__heap_end -o $BUILD_DIR/fe2-intro.wasm -Isrc -Isrc/platform/wasm src/render.c src/fmath.c src/audio.c src/fintro.c src/assets.c src/platform/wasm/mlib-wasm.c src/platform/wasm/main-wasm.c

cp data/model-overrides-le.dat $BUILD_DIR/model-overrides-le.dat
cp src/platform/wasm/fe2-intro.html $BUILD_DIR/fe2-intro.html
cp src/platform/wasm/manifest.json $BUILD_DIR/manifest.json

# Dump human readable wasm
# wasm2wat $BUILD_DIR/fe2-intro.wasm > $BUILD_DIR/fe2-intro.wat
