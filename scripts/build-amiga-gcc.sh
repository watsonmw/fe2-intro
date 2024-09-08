#!/usr/bin/env bash
mkdir -p build-amiga
set -e # stop on error
set -x # echo on

# Dump asm from exe after linking (Adding -g during compile steps improves output)
# /opt/amiga/bin/m68k-amigaos-objdump  -S --disassemble build-amiga/fintro > fintro.asm

# Dump asm from exe during compile
# /opt/amiga/bin/m68k-amigaos-gcc -S -DFINTRO_SCREEN_RES=2 -DSURFACE_HEIGHT=360 -fverbose-asm -m68040 -O3 render.c

# Compile asm source file to object
#${GCC} src/render68k.s -c -o build-amiga/render68k.o

#
# Profile driven optimizations
#
# Steps:
# 1) Build with CCFLAGS=-fprofile-generate --coverage -pg and also LDFLAGS=-lgcov (put option after .o files)
#PROFILE_OPTS="-fprofile-generate --coverage -pg"
#POST_LDFLAGS=-lgcov
# 2) Run exe over representative sample data (gcc 6 is buggy and may remove some of your code, be sure to sample data
#    covers all paths / manually test final exe)
# 3) Re-build with CCFLAGS=-fprofile-use
#PROFILE_OPTS="-fprofile-use"
#

#
# Viewing the profile info
#
# If coverage is enabled in the exe, a file '<source>.gcda' will be written for each source file when the program exits,
# this contains coverage information.  '<source>.gcno' files are built compile time and contains the branch information
# for each source file.  Both can be combined to show per line coverage, this can be viewed with gconv:
# /opt/amiga/bin/m68k-amigaos-gcov build-amiga/*.o
#
# If profiling is enabled in the exe, a file 'gmon.out' will be written when it exits, this contains function
# timing.  This can be viewed with gprof:
# /opt/amiga/bin/m68k-amigaos-gprof build-amiga/fintro-gcc build-amiga/gmon.out > gprof.txt
#

# -DM_MEM_DEBUG  - Debug heap allocations
#DEBUG_FLAGS="-g -DM_MEM_DEBUG"

# -fomit-frame-pointer  - gives a ~ 2% speedup and reduces exe size (less moves)
# -mregparm             - gives a ~ 2% speedup and reduces exe size (less moves)
# -fno-strict-aliasing  - code violates strict aliasing, but also strict aliasing generates worse code in gcc6.5 (!)
#OPT_FLAGS="-m68040 -mregparm=4 -fomit-frame-pointer -fweb -frename-registers -fno-strict-aliasing"
OPT_FLAGS="-m68040 -O1 -mregparm=4 -fomit-frame-pointer -fweb -frename-registers -fno-strict-aliasing"

GCC="/opt/amiga/bin/m68k-amigaos-gcc"
VASM="/opt/amiga/bin/vasmm68k_mot"
CCFLAGS="-DFINTRO_SCREEN_RES=2 -DSURFACE_HEIGHT=360 -DM_CLIB_DISABLE -DM_LOG_ALLOCATIONS ${DEBUG_FLAGS} ${OPT_FLAGS} ${PROFILE_OPTS}"

# Embed version info into exe for custom profiling code
BUILD_DATE=`date +"%Y/%m/%d %H:%M:%S"`
# Include git version
BUILD_ID=`git describe --abbrev=15 --dirty --always --tags`
BUILD_INFO="$BUILD_DATE\\n$BUILD_ID\\ngcc $CCFLAGS $POS_LDFLAGS"
# include diffs if any from git version
git diff src/render.c > build-amiga/source.diffs
xxd -i build-amiga/source.diffs > src/sourcediffs.h

cp data/model-overrides-le.dat build-amiga/

# Actually Compile
${GCC} src/platform/amiga/main-amiga.c $CCFLAGS -DBUILD_INFO="$BUILD_INFO" -DBUILD_ID="$BUILD_ID" -I src -c -o build-amiga/main.o
${GCC} src/platform/amiga/mlib-amiga.c $CCFLAGS -I src -c -o build-amiga/mlib-amiga.o
${GCC} src/mlib.c $CCFLAGS -I src -c -o build-amiga/mlib.o
${GCC} src/audio.c $CCFLAGS -I src -c -o build-amiga/audio.o
${GCC} src/render.c $CCFLAGS -I src -c -o build-amiga/render.o
${GCC} src/fmath.c $CCFLAGS -I src -c -o build-amiga/fmath.o
${GCC} src/fintro.c $CCFLAGS -I src -c -o build-amiga/fintro.o
${GCC} src/assets.c $CCFLAGS -I src -c -o build-amiga/assets.o
# Following could be used to compile ASM functions
# ${VASM} -m68040 -Fhunk src/render68k.asm -o build-amiga/render68k.o

# Link with -noixemul for smaller exe size, this disables the UNIX emulation layer, this can be buggy and we don't need
# it anyway.  (could use nostdlib)
POST_LDFLAGS="-noixemul"
${GCC} $CCFLAGS build-amiga/main.o build-amiga/mlib-amiga.o build-amiga/fmath.o build-amiga/fintro.o build-amiga/render.o build-amiga/assets.o build-amiga/audio.o ${POST_LDFLAGS} -o build-amiga/fintro-gcc -Xlinker -verbose
#/opt/amiga/bin/m68k-amigaos-objdump -S --disassemble build-amiga/fintro-gcc > build-amiga/fintro-gcc.asm
cp build-amiga/fintro-gcc /mnt/c/Amiga/frontier/build-amiga
