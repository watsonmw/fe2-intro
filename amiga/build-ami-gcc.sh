#!/usr/bin/env bash
mkdir -p build
set -e # stop on error
set -x # echo on

# Dump asm from exe after linking (Adding -g during compile steps improves output)
# /opt/amiga/bin/m68k-amigaos-objdump  -S --disassemble build/fintro > fintro.asm

# Dump asm from exe during compile
# /opt/amiga/bin/m68k-amigaos-gcc -S -DFINTRO_SCREEN_RES=2 -DSURFACE_HEIGHT=360 -fverbose-asm -m68040 -O3 render.c

# Compile asm source file to object
#${GCC} src/render68k.s -c -o build/render68k.o

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
# /opt/amiga/bin/m68k-amigaos-gcov build/*.o
#
# If profiling is enabled in the exe, a file 'gmon.out' will be written when it exits, this contains function
# timing.  This can be viewed with gprof:
# /opt/amiga/bin/m68k-amigaos-gprof build/fintro-gcc build/gmon.out > gprof.txt
#

#
# -mregparm - gives a 2% speedup and reduces exe size (less moves)
# -fomit-frame-pointer? - gives a 2% speedup and reduces exe size (less moves)
#

GCC="/opt/amiga/bin/m68k-amigaos-gcc"
VASM="/opt/amiga/bin/vasmm68k_mot"
CCFLAGS="-m68040 -O3 -DFINTRO_SCREEN_RES=2 -DSURFACE_HEIGHT=360 $PROFILE_OPTS -mregparm=4 -fomit-frame-pointer -fweb -frename-registers"

# Embed version info into exe for custom profiling code
BUILD_DATE=`date +"%Y/%m/%d %H:%M:%S"`
# Include git version
BUILD_ID=`git describe --abbrev=15 --dirty --always --tags`
BUILD_INFO="$BUILD_DATE\\n$BUILD_ID\\ngcc $CCFLAGS $POS_LDFLAGS"
# include diffs if any from git version
git diff src/render.c > build/source.diffs
xxd -i build/source.diffs > src/sourcediffs.h

# Actually Compile
${GCC} src/main-amiga.c $CCFLAGS -DBUILD_INFO="$BUILD_INFO" -DBUILD_ID="$BUILD_ID" -I src -c -o build/main.o
${GCC} src/audio.c $CCFLAGS -I src -c -o build/audio.o
${GCC} src/mlib.c $CCFLAGS -I src -c -o build/mlib.o
${GCC} src/render.c $CCFLAGS -I src -c -o build/render.o
${GCC} src/fmath.c $CCFLAGS -I src -c -o build/fmath.o
${GCC} src/fintro.c $CCFLAGS -I src -c -o build/fintro.o
${GCC} src/assets.c $CCFLAGS -I src -c -o build/assets.o
# Following could be used to compile ASM functions
# ${VASM} -m68040 -Fhunk src/render68k.asm -o build/render68k.o

# Link with -noixemul for smaller exe size, this disables the UNIX emulation layer, this can be buggy and we don't need
# it anyway.
${GCC} -lm -noixemul $CCFLAGS build/main.o build/mlib.o build/fmath.o build/fintro.o build/render.o build/assets.o build/audio.o ${POST_LDFLAGS} -o build/fintro-gcc
