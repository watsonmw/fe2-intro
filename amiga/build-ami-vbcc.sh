#!/usr/bin/env bash
mkdir -p build
set -e # stop on error
set -x # echo on

export VBCC="/amiga/vbcc/amiga_sdk/vbcc"
export PATH=${VBCC}/bin:${PATH}
export NDK_INC="/amiga/vbcc/amiga_sdk/NDK_3.9/Include/include_h"
export OPTS="+aos68k -maxoptpasses=100 -c99 -O4 -cpu=68040 -DAMIGA -DFINTRO_SCREEN_RES=2 -DSURFACE_HEIGHT=360 -I$NDK_INC -I/amiga/vbcc/amiga_sdk/CGraphX/C/Include"
export VC="${VBCC}/bin/vc"

${VC} src/render.c $OPTS -I src -c -o build/render.o
${VC} src/audio.c $OPTS -I src -c -o build/audio.o
${VC} src/main-amiga.c $OPTS -I src -c -o build/main.o
${VC} src/mlib.c $OPTS -I src -c -o build/mlib.o
${VC} src/fmath.c $OPTS -I src -c -o build/fmath.o
${VC} src/fintro.c $OPTS -I src -c -o build/fintro.o
${VC} src/assets.c $OPTS -I src -c -o build/assets.o
${VC} ${OPTS} -lamiga -lmieee -final build/main.o build/mlib.o build/fmath.o build/fintro.o build/render.o build/assets.o build/audio.o -o build/fintro-vbcc
