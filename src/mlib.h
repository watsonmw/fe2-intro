#ifndef MLIB_H
#define MLIB_H

//
// Public domain c library for the following:
//
// - heap debugging
// - c arrays
// - memory/file reading / writing framework
// - ini reading
//
// You might want check out STB libs and GCC heap debug options before using this, this is just my personal version that
// integrates custom heap debugging/tracking into the array functionality.
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef char i8;
typedef unsigned char u8;
typedef short i16;
typedef unsigned short u16;
typedef int i32;
typedef int b32;
typedef unsigned u32;
typedef long long i64;
typedef unsigned long long u64;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Mark function as internal (used to distinguish between variables that are static)
#define MINTERNAL static

// Mark function as internal and hint to compiler to make it inline (typically gcc will already once marked
// internal, if appropriate, without having to add 'inline')
#define MINLINE static inline

#ifdef __cplusplus
extern "C" {
#endif

//////////////////////////////////////////////////////////
// Heap debugging
// As an alternative GCC has good compile time options for similar checks

#ifdef MEMDEBUG

#define MMalloc(size) MMemDebugAlloc(size)
#define MFree(p) MMemDebugFree(p)
#define MRealloc(p, size) MMemDebugRealloc(p, size)

#define MEMDEBUG_SOURCE_DEFINE const char* file, int line,
#define MEMDEBUG_SOURCE_PASS file, line,
#define MEMDEBUG_SOURCE_MACRO __FILE__, __LINE__,

#else

// When debug mode is off

#ifndef MMalloc
#define MMalloc(size) malloc(size)
#endif

#ifndef MFree
#define MFree(p) free(p)
#endif

#ifndef MRealloc
#define MRealloc(p, size) realloc(p, size)
#endif

#define MEMDEBUG_SOURCE_DEFINE
#define MEMDEBUG_SOURCE_PASS
#define MEMDEBUG_SOURCE_MACRO

#endif

void MMemDebugInit(void);
void MMemDebugDeinit(void);

void* _MMemDebugAlloc(const char* file, int line, size_t size);
#define MMemDebugAlloc(size) _MMemDebugAlloc(__FILE__, __LINE__, size)

void* _MMemDebugRealloc(const char* file, int line, void* p, size_t size);
#define MMemDebugRealloc(p, size) _MMemDebugRealloc(__FILE__, __LINE__, p, size)

void MMemDebugFree(void* p);
b32 MMemDebugCheck(void* p);

b32 MMemDebugCheckAll(void);

/////////////////////////////////////////////////////////
// Math
#define MSWAP(x, y, T) { T SWAP = x; x = y; y = SWAP; }

/////////////////////////////////////////////////////////
// Endian conversions
// 16 flip: ((t & 0xff) << 8) + ((t >> 8) & 0xff)
// 32 flip: ((t & 0xff) << 24) + ((t >> 24) & 0xff) + ((t >> 16 & 0xff) << 8) + (((t >> 8) & 0xff) << 16);
#ifdef __GNUC__
#define MSTRUCTPACKED __attribute__((packed))
#if defined(__i386__) || defined(__x86_64)
#define MBIGENDIAN16(x) __builtin_bswap16(x)
#define MBIGENDIAN32(x) __builtin_bswap32(x)
#define MLITTLEENDIAN16(x) x
#define MLITTLEENDIAN32(x) x
#define MENDIANSWAP16(x) __builtin_bswap16(x)
#define MENDIANSWAP32(x) __builtin_bswap32(x)
#else
#define MBIGENDIAN16(x) x
#define MBIGENDIAN32(x) x
#define MLITTLEENDIAN16(x) __builtin_bswap16(x)
#define MLITTLEENDIAN32(x) __builtin_bswap32(x)
#define MENDIANSWAP16(x) __builtin_bswap16(x)
#define MENDIANSWAP32(x) __builtin_bswap32(x)
#endif
#elif defined(_MSC_VER)
#define MSTRUCTPACKED
#define MBIGENDIAN16(x) _byteswap_ushort(x)
#define MBIGENDIAN32(x) _byteswap_ulong(x)
#define MLITTLEENDIAN16(x) x
#define MLITTLEENDIAN32(x) x
#define MENDIANSWAP16(x) _byteswap_ushort(x)
#define MENDIANSWAP32(x) _byteswap_ulong(x)
#elif defined(__VBCC__)
#define MSTRUCTPACKED
#define MBIGENDIAN16(x) x
#define MBIGENDIAN32(x) x
#define MLITTLEENDIAN16(t) ((t & 0xff) << 8) + ((t >> 8) & 0xff)
#define MLITTLEENDIAN32(t) ((t & 0xff) << 24) + ((t >> 24) & 0xff) + ((t >> 16 & 0xff) << 8) + (((t >> 8) & 0xff) << 16)
#define MENDIANSWAP16(t) ((t & 0xff) << 8) + ((t >> 8) & 0xff)
#define MENDIANSWAP32(t) ((t & 0xff) << 24) + ((t >> 24) & 0xff) + ((t >> 16 & 0xff) << 8) + (((t >> 8) & 0xff) << 16)
#endif

/////////////////////////////////////////////////////////
// Logging

void MLogf(const char *format, ...);
void MLogfNoNewLine(const char *format, ...);
void MLog(const char *str);

// Log / trigger debugger
#ifdef MASSERT
#ifdef __GNUC__
#if defined(__i386__) || defined(__x86_64)
MINLINE void MBreakpoint(const char* str) {
    MLog(str);
    asm("int $3");
}
#endif
#endif
#else
#define MBreakpoint(str)
#endif

// Quote the given #define contents, this is useful to print out the contents of a macro / #define.
// E.g. if BUILD_INFO is passed in to compiler from command line with -DBUILD_INFO=<build info>, you
// can add the following to the C file to allow the version to be printed out / pulled from static
// data in final exe:
//   const char* BUILD_STRING = MMACRO_QUOTE(BUILD_INFO);
#define __MMACRO_QUOTE(name) #name
#define MMACRO_QUOTE(name) __MMACRO_QUOTE(name)

/////////////////////////////////////////////////////////
// Memory reading / writing
//
typedef struct {
    u8* pos;  // current pos in memory buffer
    u8* mem;  // pointer to start of memory buffer
    u32 size; // current size read or written (pos - mem)
    u32 capacity; // size of memory buffer allocated
} MMemIO;

// Initialise MMemIO to write to existing memory
// size is set to zero and pos set to 'mem'
void MMemInit(MMemIO* memIO, u8* mem, u32 capacity);

// Initialise MMemIO to read from existing memory
// capacity and size are initialized to the same value
// and pos is the start of memory
void MMemReadInit(MMemIO* memIO, u8* mem, u32 size);

// Allocate the given amount of memory and
void MMemInitAlloc(MMemIO* memIO, u32 size);

MINTERNAL void MMemReset(MMemIO* memIO) {
    memIO->size = 0;
    memIO->pos = memIO->mem;
}

MINTERNAL void MMemFree(MMemIO* memIO) {
    MFree(memIO->mem);
}

void MMemAddBytes(MMemIO* memIO, u32 size);

void MMemAddBytesZero(MMemIO* memIO, u32 size);

// --- Writing ---
// Write data at current pos and advance.
// These are often slower than writing directly as they do endian byte swaps and allow writing at byte offsets.
// For speed call MMemAddBytes() and write data directly to *pos, making sure the offsets are aligned.
void MMemWriteU16LE(MMemIO* memIO, u16 val);

void MMemWriteU16BE(MMemIO* memIO, u16 val);

void MMemWriteU32LE(MMemIO* memIO, u32 val);

void MMemWriteU32BE(MMemIO* memIO, u32 val);

void MMemWriteU8CopyN(MMemIO* writer, u8* src, u32 size);

void MMemWriteI8CopyN(MMemIO* writer, i8* src, u32 size);

// --- Reading ---
// Read data at current pos and advance.
i32 MMemReadI8(MMemIO* reader, i8* val);
i32 MMemReadU8(MMemIO* reader, u8* val);

i32 MMemReadI16(MMemIO* reader, i16* val);
i32 MMemReadI16BE(MMemIO* reader, i16* val);
i32 MMemReadI16LE(MMemIO* reader, i16* val);
i32 MMemReadU16(MMemIO* reader, u16* val);
i32 MMemReadU16BE(MMemIO* reader, u16* val);
i32 MMemReadU16LE(MMemIO* reader, u16* val);

i32 MMemReadI32(MMemIO* reader, i32* val);
i32 MMemReadI32BE(MMemIO* reader, i32* val);
i32 MMemReadI32LE(MMemIO* reader, i32* val);
i32 MMemReadU32(MMemIO* reader, u32* val);
i32 MMemReadU32BE(MMemIO* reader, u32* val);
i32 MMemReadU32LE(MMemIO* reader, u32* val);

i32 MMemReadU8CopyN(MMemIO* reader, u8* dst, u32 size);

i32 MMemReadI8CopyN(MMemIO* reader, i8* dst, u32 size);

// Read a string
char* MMemReadStr(MMemIO* reader);

MINTERNAL b32 MMemReadDone(MMemIO* reader) {
    u32 read = reader->pos - reader->mem;
    if (read >= reader->size) {
        return 1;
    } else {
        return 0;
    }
}

/////////////////////////////////////////////////////////
// Dynamic Array
// Pretty much the stb lib array type, but integrates with heap debug functionality above.

typedef struct {
    u32 size; // number of elements currently used
    u32 capacity; // total capacity in array elements
} MArrayInfo;

#define MARRAY_TYPEDEF(TYPE, MArrayTypeName) typedef struct MArrayTypeName { \
        TYPE* arr; \
        MArrayInfo p; \
    } MArrayTypeName;

// Initialize the array after it is defined
#define MArrayInit(a) M_ArrayInit((void**)&(a).arr, &(a).p)

// Use this to free the memory allocated
#define MArrayFree(a) M_ArrayFree((void**)&(a).arr, &(a).p)

// Add value to end of array
#define MArrayAdd(a, v) (M_ArrayGrowIfNeeded(MEMDEBUG_SOURCE_MACRO M_ArrayUnpack(a), 1) ? (((a).arr[(a).p.size++] = (v)),0) : 0)

// Add space for item to end of array and return ptr to it
#define MArrayAddPtr(a) (M_ArrayGrowIfNeeded(MEMDEBUG_SOURCE_MACRO M_ArrayUnpack(a), 1) ? (((a).arr + (a).p.size++)) : 0)

#define MArrayGrow(a, newSize) (M_ArrayGrowIfNeeded(MEMDEBUG_SOURCE_MACRO M_ArrayUnpack(a), newSize))

#define MArrayInsert(a, i, v) (M_ArrayInsertSpace(MEMDEBUG_SOURCE_MACRO M_ArrayUnpack(a), i) ? (((a).arr[i] = (v)), 0) : 0)

#define MArraySet(a, i, v) (M_ArrayGrowAndClearIfNeeded(MEMDEBUG_SOURCE_MACRO M_ArrayUnpack(a), i + 1) ? (((a).arr[i] = (v)), 0) : 0)

#define MArrayResizeNewCopy(a, newSize) (M_ArrayResizeNewCopy(MEMDEBUG_SOURCE_MACRO M_ArrayUnpack(a), newSize))

// Get value at index
#define MArrayGet(a, idx) ((a).arr[idx])

// Get ptr to value at index
#define MArrayGetPtr(a, i) ((a).arr + i)

#define MArraySize(a) ((a).p.size)

#define MArrayPop(a) ((a).arr[--(a).p.size])

#define MArrayTop(a) ((a).arr[(a).p.size - 1])

#define MArrayTopPtr(a) ((a).arr + ((a).p.size - 1))

#define MArrayClear(a) ((a).p.size = 0)

#define MArrayCopy(src, dest) (M_ArrayCopy(MEMDEBUG_SOURCE_MACRO M_ArrayUnpack(src), (void**)&(dest).arr, &(dest).p))

// Helper macro to convert array type to (void** arr, MArray* md, u32 itemsize)
#define M_ArrayUnpack(a) (void**)&(a).arr, &(a).p, (u32)(sizeof((a).arr[0]))

// Grow array to have enough space for at least minNeeded elements
// If it fails (OOM), the array will be deleted, a.p will be NULL, a.md.cap and a.md.cnt will be 0
// and the functions returns 0; else (on success) it returns 1
i32 M_ArrayGrow(MEMDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize, u32 minNeeded);

void M_ArrayFree(void** arr, MArrayInfo* p);

MINLINE void M_ArrayInit(void** arr, MArrayInfo* p) {
    *arr = 0;
    p->size = 0;
    p->capacity = 0;
}

MINLINE i32 M_ArrayGrowIfNeeded(MEMDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize, u32 numAdd) {
    u32 needed = p->size + numAdd;
    if (p->capacity >= needed) {
        return 1;
    } else {
        return M_ArrayGrow(MEMDEBUG_SOURCE_PASS arr, p, itemSize, needed);
    }
}

MINLINE i32 M_ArrayCopy(MEMDEBUG_SOURCE_DEFINE void** srcArr, MArrayInfo* srcInfo, u32 itemSize,
        void** destArr, MArrayInfo* destInfo) {

    M_ArrayGrowIfNeeded(MEMDEBUG_SOURCE_PASS destArr, destInfo, itemSize, srcInfo->size);
    destInfo->size = srcInfo->size;
    memcpy(*destArr, *srcArr, itemSize * destInfo->size);
    return 1;
}

MINLINE i32 M_ArrayGrowAndClearIfNeeded(MEMDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize, u32 newSize) {
    if (p->size >= newSize) {
        return 1;
    }

    if (p->capacity >= newSize) {
        u32 oldSize = p->size;
        memset((u8*)(*arr) + (oldSize * itemSize), 0, (newSize - oldSize) * itemSize);
        p->size = newSize;
        return 1;
    } else {
        u32 oldSize = p->size;
        i32 r = M_ArrayGrow(MEMDEBUG_SOURCE_PASS arr, p, itemSize, newSize);
        memset((u8*)(*arr) + (oldSize * itemSize), 0, (newSize - oldSize) * itemSize);
        p->size = newSize;
        return r;
    }
}

MINLINE void* M_ArrayResizeNewCopy(MEMDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize, u32 newSize) {
    u32 needed = newSize * itemSize;
    void* old = arr;
#ifdef MEMDEBUG
    *arr = _MMemDebugAlloc(file, line, needed);
#else
    *arr = MMalloc(needed);
#endif
    p->size = needed;

    return old;
}

MINLINE i32 M_ArrayInsertSpace(MEMDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize, u32 i) {
    i32 r = M_ArrayGrowIfNeeded(MEMDEBUG_SOURCE_PASS arr, p, itemSize, 1);
    if (!r) {
        return r;
    }
    if (p->size != i) {
        u8* ptr = (u8*)*arr;
        u8* src = ptr + itemSize * i;
        u8* dest = src + itemSize;
        u32 size = (p->size - i) * itemSize;
        memmove(dest, src, size);
    }
    p->size++;
    return 1;
}

MARRAY_TYPEDEF(u32, u32Array)
MARRAY_TYPEDEF(u64, u64Array)

/////////////////////////////////////////////////////////
// File Reading / Writing

typedef struct {
    u8* data;
    u32 size;
} MReadFileRet;

#define MFileReadFully(filePath) (MFileReadFullyWithOffset(filePath, 0, 0))

#define MFileReadFullyWithSize(filePath, maxSize) (MFileReadFullyWithOffset(filePath, maxSize, 0))

MReadFileRet MFileReadFullyWithOffset(const char* filePath, u32 maxSize, u32 offset);

i32 MFileWriteDataFully(const char* filePath, u8* data, u32 size);

typedef struct {
    u64 size;
    u32 exists;
    u64 lastModifiedTime;
} MFileInfo;

MFileInfo MGetFileInfo(const char* filePath);

typedef struct {
    void* handle;
    u32 open;
} MFile;

MFile MFileWriteOpen(const char* filePath);
i32 MFileWriteData(MFile* file, u8* data, u32 size);
MINLINE i32 MFileWriteMem(MFile* file, MMemIO* mem) {
    return MFileWriteData(file,  mem->mem, mem->pos - mem->mem);
}
void MFileClose(MFile* file);

/////////////////////////////////////////////////////////
// String parsing / conversion functions

enum MParse {
    MParse_SUCCESS = 0,
    MParse_NOT_A_NUMBER = -1,
};

i32 MParseI32(const char* start, const char* end, i32* out);
i32 MParseI32Hex(const char* start, const char* end, i32* out);
i32 MStrCmp(const char* str1, const char* str2);
i32 MStrCmp3(const char* str1, const char* str2Start, const char* str2End);
void MStrU32ToBinary(u32 val, i32 size, char* out);

// Get pointer to end of str (scan for zero).  This is useful for functions that take a function end.
const char* MStrEnd(const char* str);

MINTERNAL u32 MStrLen(const char* str) {
    u32 n = 0;
    while (*str++) n++;
    return n;
}

i32 MStringAppend(MMemIO* memIo, const char* str);
i32 MStringAppendf(MMemIO* memIo, const char* format, ...);

/////////////////////////////////////////////////////////
// Simple ini file reading / Writing
// ini scopes not supported

typedef struct {
    char* key;
    u32 keySize;

    char* val;
    u32 valSize;
} MIniPair;

MARRAY_TYPEDEF(MIniPair, MIniPairs)

typedef struct {
    u8* data;
    u32 dataSize;
    MIniPairs values;
    u8 owned; // set if data is to free'd when MIniFree() is called
} MIni;

i32 MIniLoadFile(MIni* ini, const char* filePath);

i32 MIniParse(MIni* ini);

i32 MIniFree(MIni* ini);

i32 MIniReadI32(MIni* ini, const char* var, i32* valOut);

typedef struct {
    void* fileHandle;
} MIniSave;

i32 MIniSaveMWriteI32(MIniSave* ini, const char* key, i32 value);

i32 MIniSaveFile(MIniSave* ini, const char* filePath);

void MIniSaveFree(MIniSave* ini);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
