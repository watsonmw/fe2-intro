#!/usr/bin/env bash

# Not tested with current code
mkdir -p build-amiga
set -e # stop on error
set -x # echo on

export VBCC="/amiga/vbcc/amiga_sdk/vbcc"
export PATH=${VBCC}/bin:${PATH}
export NDK_INC="/amiga/vbcc/amiga_sdk/NDK_3.9/Include/include_h"
export OPTS="+aos68k -maxoptpasses=100 -c99 -O4 -cpu=68040 -DAMIGA -DFINTRO_SCREEN_RES=2 -DSURFACE_HEIGHT=360 -I$NDK_INC -I/amiga/vbcc/amiga_sdk/CGraphX/C/Include"
export VC="${VBCC}/bin/vc"

${VC} src/render.c $OPTS -I src -c -o build-amiga/render.o
${VC} src/audio.c $OPTS -I src -c -o build-amiga/audio.o
${VC} src/main-amiga.c $OPTS -I src -c -o build-amiga/main.o
${VC} src/mlib.c $OPTS -I src -c -o build-amiga/mlib.o
${VC} src/fmath.c $OPTS -I src -c -o build-amiga/fmath.o
${VC} src/fintro.c $OPTS -I src -c -o build-amiga/fintro.o
${VC} src/assets.c $OPTS -I src -c -o build-amiga/assets.o
${VC} ${OPTS} -lamiga -lmieee -final build-amiga/main.o build-amiga/mlib.o build-amiga/fmath.o build-amiga/fintro.o build-amiga/render.o build-amiga/assets.o build-amiga/audio.o -o build-amiga/fintro-vbcc
