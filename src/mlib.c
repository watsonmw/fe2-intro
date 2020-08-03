#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef USE_SDL
#include <SDL2/SDL.h>
#endif

#ifdef __GNUC__
#include <sys/stat.h>
#endif

#include "mlib.h"

/////////////////////////////////////////////////////////
// Memory overwrite checking

#define SENTINEL_BEFORE 0x1000
#define SENTINEL_AFTER 0x1000

typedef struct sMMemAllocation {
    size_t size;
    u8* start;
    u8* mem;
    const char* file;
    int line;
} MMemAllocInfo;

MARRAY_TYPEDEF(MMemAllocInfo, MMemAllocations)
MARRAY_TYPEDEF(u32, MMemAllocationFreeSpots)

static MMemAllocations sMemAllocInfo;
static MMemAllocationFreeSpots sMemAllocFree;
static b32 sMemDebugInitialized = FALSE;

MINLINE i32 MMemDebug_ArrayGrowIfNeeded(void** arr, MArrayInfo* p, u32 itemSize, u32 numAdd) {
    u32 minNeeded = p->size + numAdd;
    if (p->capacity >= minNeeded) {
        return 1;
    } else {
        u32 capacity = p->capacity;
        u32 newCapacity = (capacity > 4) ? (2 * capacity) : 8; // allocate for at least 8 elements

        if (minNeeded > newCapacity) {
            newCapacity = minNeeded;
        }

        void* mem = realloc(*arr, itemSize * newCapacity);
        if (mem == NULL)  {
            MLogf("MArray realloc failed for %x", *arr);
            return 0;
        }
        *arr = mem;

        if (*arr) {
            p->capacity = newCapacity;
        } else {
            p->capacity = 0;
            p->size = 0;

            return 0;
        }

        return 1;
    }
}

#define MMemDebugArrayAddPtr(a) (MMemDebug_ArrayGrowIfNeeded(M_ArrayUnpack(a), 1) ? (((a).arr + (a).p.size++)) : 0)

void MMemDebugInit() {
    if (!sMemDebugInitialized) {
        MArrayInit(sMemAllocInfo);
        MArrayInit(sMemAllocFree);
        sMemDebugInitialized = TRUE;
    }
}

void MMemDebugDeinit() {
    if (!sMemDebugInitialized) {
        return;
    }

    MMemDebugCheckAll();

    for (u32 i = 0; i < MArraySize(sMemAllocInfo); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemAllocInfo, i);
        if (memAlloc->start) {
            MLogf("Leaking allocation: %s:%d 0x%x (%d)", memAlloc->file, memAlloc->line, memAlloc->start, memAlloc->size);
        }
    }

    free(sMemAllocInfo.arr); sMemAllocInfo.arr = NULL;
    sMemAllocInfo.p.capacity = 0;
    sMemAllocInfo.p.size = 0;

    free(sMemAllocFree.arr); sMemAllocFree.arr = NULL;
    sMemAllocFree.p.capacity = 0;
    sMemAllocFree.p.size = 0;

    sMemDebugInitialized = FALSE;
}

static u8 sMagicSentinelValues[4] = { 0x1d, 0xdf, 0x83, 0xc7 };

void MMemDebugSentinelSet(void* mem, size_t size) {
    u8* ptr = (u8*)mem;

    for (u32 i = 0; i < size; ++i) {
        ptr[i] = sMagicSentinelValues[i & 0x3];
    }
}

u32 MMemDebugSentinelCheck(void* mem, size_t size) {
    u8* ptr = (u8*)mem;
    u32 bytesOverwritten = 0;


    for (u32 i = 0; i < size; ++i) {
        if (ptr[i] != sMagicSentinelValues[i & 0x3]) {
            bytesOverwritten++;
        }
    }

    return bytesOverwritten;
}

static const char* sHexChars = "0123456789abcdef";

static void LogBytesOverwritten(u8* start, u8* ptr, u32 bytesOverwritten) {
    MLogf("offset: %d bytes: %d", start - ptr, bytesOverwritten);

    const int bytesPerLine = 16; // must be power of two - see AND (&) below
    char buff[bytesPerLine + 1];
    int buffPos = 0;

    for (int j  = 0; j < bytesOverwritten; ++j) {
        u8 val = start[j];
        buff[buffPos++] = sHexChars[val >> 4];
        buff[buffPos++] = sHexChars[val & 0xf];
        if ((buffPos & (bytesPerLine - 1)) == 0) {
            buff[buffPos] = 0;
            MLog(buff);
            buffPos = 0;
        }
    }

    if (buffPos) {
        buff[buffPos] = 0;
        MLog(buff);
    }
}

void MMemDebugSentinelCheckMLog(void* mem, size_t size) {
    u8* ptr = (u8*)mem;
    u32 bytesOverwritten = 0;

    int state = 0;
    u8* start = 0;
    for (u32 i = 0; i < size; ++i) {
        if (ptr[i] != sMagicSentinelValues[i & 0x3]) {
            if (state == 0) {
                start = ptr + i;
                state = 1;
                bytesOverwritten = 0;
            }
            bytesOverwritten++;
        } else if (state == 1) {
            LogBytesOverwritten(start, ptr, bytesOverwritten);
            state = 0;
        }
    }

    if (state == 1) {
        LogBytesOverwritten(start, ptr, bytesOverwritten);
    }
}

b32 MMemDebugCheckMemAlloc(MMemAllocInfo* memAlloc) {
    if (!memAlloc->mem) {
        return TRUE;
    }

    u32 beforeBytes = MMemDebugSentinelCheck(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    u32 afterBytes = MMemDebugSentinelCheck(memAlloc->mem + memAlloc->size, SENTINEL_AFTER);
    if (beforeBytes) {
        MLogf("MMemDebugCheck: %s:%d : %d bytes overwritten before", memAlloc->file, memAlloc->line, beforeBytes);
        MMemDebugSentinelCheckMLog(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
        MMemDebugSentinelSet(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    }

    if (afterBytes) {
        MLogf("MMemDebugCheck: %s:%d : %d bytes overwritten after", memAlloc->file, memAlloc->line, afterBytes);
        MMemDebugSentinelCheckMLog(memAlloc->mem + memAlloc->size, SENTINEL_AFTER);
        MMemDebugSentinelSet(memAlloc->mem + memAlloc->size, SENTINEL_AFTER);
    }

    return afterBytes & beforeBytes;
}

void* _MMemDebugAlloc(const char* file, int line, size_t size) {
    if (!sMemDebugInitialized) {
        MMemDebugInit();
        atexit(MMemDebugDeinit);
    }

    MMemAllocInfo* memAlloc = NULL;
    if (MArraySize(sMemAllocFree)) {
        memAlloc = MArrayGetPtr(sMemAllocInfo, MArrayPop(sMemAllocFree));
    } else {
        memAlloc = MMemDebugArrayAddPtr(sMemAllocInfo);
    }

    size_t start = SENTINEL_BEFORE + 15;
    size_t allocSize = start + size + SENTINEL_AFTER;
    memAlloc->start = (u8*)malloc(allocSize);
    memAlloc->size = size;
    memAlloc->file = file;
    memAlloc->line = line;
    memAlloc->mem = (u8*) (((size_t) (memAlloc->start + start)) & ~((size_t)15u));

    MMemDebugSentinelSet(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    MMemDebugSentinelSet(memAlloc->mem + memAlloc->size, SENTINEL_AFTER);

    return memAlloc->mem;
}

void MMemDebugFree(void* p) {
    if (p == NULL) {
        return;
    }

    for (u32 i = 0; i < MArraySize(sMemAllocInfo); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemAllocInfo, i);
        if (p == memAlloc->mem) {
            MMemDebugCheckMemAlloc(memAlloc);
            free(memAlloc->start);
            memAlloc->start = 0;
            memAlloc->mem = 0;
            *(MMemDebugArrayAddPtr(sMemAllocFree)) = i;
            return;
        }
    }

    MLog("MMemDebugFree called on invalid ptr");
}

void* _MMemDebugRealloc(const char* file, int line, void* p, size_t size) {
    if (p == NULL) {
        return _MMemDebugAlloc(file, line, size);
    }

    MMemAllocInfo* memAllocFound = NULL;
    for (u32 i = 0; i < MArraySize(sMemAllocInfo); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemAllocInfo, i);
        if (p == memAlloc->mem) {
            MMemDebugCheckMemAlloc(memAlloc);
            memAllocFound = memAlloc;
            break;
        }
    }

    if (memAllocFound == NULL) {
        MLogf("MMemDebugRealloc called on invalid ptr at line: %s:%d", file, line);
        return p;
    }

    size_t start = SENTINEL_BEFORE + 15;
    size_t allocSize = start + size + SENTINEL_AFTER;

    memAllocFound->size = size;
    u8* newStart = (u8*)realloc(memAllocFound->start, allocSize);
    u8* newMem = (u8*) (((size_t) (newStart + start)) & ~((size_t)15u));
    i64 newOffset = newMem - newStart;
    i64 oldOffset = memAllocFound->mem - memAllocFound->start;
    if (newOffset != oldOffset) {
        memmove(newMem, newStart + oldOffset, size);
    }
    memAllocFound->mem = newMem;
    memAllocFound->start = newStart;
    memAllocFound->file = file;
    memAllocFound->line = line;

    MMemDebugSentinelSet(memAllocFound->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    MMemDebugSentinelSet(memAllocFound->mem + memAllocFound->size, SENTINEL_AFTER);

    return memAllocFound->mem;
}

b32 MMemDebugCheck(void* p) {
    MMemAllocInfo* memAllocFound = NULL;
    for (u32 i = 0; i < MArraySize(sMemAllocInfo); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemAllocInfo, i);
        if (p == memAlloc->mem) {
            MMemDebugCheckMemAlloc(memAlloc);
            memAllocFound = memAlloc;
            break;
        }
    }

    if (memAllocFound == NULL) {
        MLog("MMemDebugCheck called on invalid ptr");
        return FALSE;
    }

    return MMemDebugCheckMemAlloc(memAllocFound);
}

b32 MMemDebugCheckAll() {
    b32 memOk = TRUE;
    for (u32 i = 0; i < MArraySize(sMemAllocInfo); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemAllocInfo, i);
        if (!MMemDebugCheckMemAlloc(memAlloc)) {
            memOk = FALSE;
        }
    }

    return memOk;
}

/////////////////////////////////////////////////////////
// Logging

void MLogf(const char *format, ...) {
    va_list vargs;
    va_start(vargs, format);
    vprintf(format, vargs);
    printf("\n");
    fflush(stdout);
    va_end(vargs);
}

void MLogfNoNewLine(const char *format, ...) {
    va_list vargs;
    va_start(vargs, format);
    vprintf(format, vargs);
    fflush(stdout);
    va_end(vargs);
}

void MLog(const char *str) {
    printf("%s\n", str);
    fflush(stdout);
}

/////////////////////////////////////////////////////////
// Dynamic Array

i32 M_ArrayGrow(MEMDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize, u32 minNeeded) {
    u32 capacity = p->capacity;
    u32 newCapacity = (capacity > 4) ? (2 * capacity) : 8; // allocate for at least 8 elements

    if (minNeeded > newCapacity) {
        newCapacity = minNeeded;
    }

#ifdef MEMDEBUG
    void* mem = _MMemDebugRealloc(file, line, *arr, itemSize * newCapacity);
#else
    void* mem = MRealloc(*arr, itemSize * newCapacity);
#endif
    if (mem == NULL)  {
        MLogf("MArray realloc failed for %x", *arr);
        return 0;
    }
    *arr = mem;

    if (*arr) {
        p->capacity = newCapacity;
    } else {
        p->capacity = 0;
        p->size = 0;

        return 0;
    }

    return 1;
}

void M_ArrayFree(void** arr, MArrayInfo* p) {
    MFree(*arr); *arr = NULL;
    p->capacity = 0;
    p->size = 0;
}

/////////////////////////////////////////////////////////
// Mem buffer reading / writing

void M_MemResize(MMemIO* memIO, u32 newMinSize) {
    u32 newSize = newMinSize;
    if (memIO->capacity * 2 > newSize) {
        newSize = memIO->capacity * 2;
    }
    memIO->mem = MRealloc(memIO->mem, newSize);
    memIO->pos = memIO->mem + memIO->size;
    memIO->capacity = newSize;
}

void MMemInitAlloc(MMemIO* memIO, u32 size) {
    u8* mem = (u8*)MMalloc(size);
    MMemInit(memIO, mem, size);
}

void MMemInit(MMemIO* memIO, u8* mem, u32 size) {
    memIO->pos = mem;
    memIO->mem = mem;
    memIO->capacity = size;
    memIO->size = 0;
}

void MMemReadInit(MMemIO* memIO, u8* mem, u32 size) {
    memIO->pos = mem;
    memIO->mem = mem;
    memIO->capacity = size;
    memIO->size = size;
}

void MMemAddBytes(MMemIO* memIO, u32 size) {
    i32 newSize = memIO->size + size;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    memIO->pos += size;
    memIO->size = newSize;
}

void MMemAddBytesZero(MMemIO* memIO, u32 size) {
    u32 newSize = memIO->size + size;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    memset(memIO->pos, 0, size);
    memIO->pos += size;
    memIO->size = newSize;
}

void MMemWriteU16BE(MMemIO* memIO, u16 val) {
    u32 newSize = memIO->size + 2;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    *(memIO->pos++) = (val >> 8);
    *(memIO->pos++) = (val & 0xff);

    memIO->size = newSize;
}

void MMemWriteU16LE(MMemIO* memIO, u16 val) {
    u32 newSize = memIO->size + 2;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    *(memIO->pos++) = (val & 0xff);
    *(memIO->pos++) = (val >> 8);

    memIO->size = newSize;
}

void MMemWriteU32BE(MMemIO* memIO, u32 val) {
    u32 newSize = memIO->size + 4;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    *(memIO->pos++) = (val >> 24);
    *(memIO->pos++) = (val >> 16) & 0xff;
    *(memIO->pos++) = (val >> 8) & 0xff;
    *(memIO->pos++) = (val & 0xff);

    memIO->size = newSize;
}

void MMemWriteU32LE(MMemIO* memIO, u32 val) {
    u32 newSize = memIO->size + 4;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    *(memIO->pos++) = (val & 0xff);
    *(memIO->pos++) = (val >> 8) & 0xff;
    *(memIO->pos++) = (val >> 16) & 0xff;
    *(memIO->pos++) = (val >> 24);

    memIO->size = newSize;
}

void MMemWriteU8CopyN(MMemIO* memIO, u8* src, u32 size) {
    if (size == 0) {
        return;
    }

    u32 newSize = memIO->size + size;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    memcpy(memIO->pos, src, size);

    memIO->pos += size;
    memIO->size = newSize;
}

void MMemWriteI8CopyN(MMemIO* memIO, i8* src, u32 size) {
    if (size == 0) {
        return;
    }

    u32 newSize = memIO->size + size;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    memcpy(memIO->pos, src, size);

    memIO->pos += size;
    memIO->size = newSize;
}

i32 MMemReadI8(MMemIO* memIO, i8* val) {
    if (memIO->pos >= memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *((i8*)(memIO->pos++));

    return 0;
}

i32 MMemReadU8(MMemIO* memIO, u8* val) {
    if (memIO->pos >= memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *(memIO->pos++);

    return 0;
}

i32 MMemReadI16(MMemIO* memIO, i16* val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *((i16*)memIO->pos);

    memIO->pos += 2;

    return 0;
}

i32 MMemReadI16BE(MMemIO* memIO, i16* val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    i16 v = *((i16*)memIO->pos);
    *(val) = MLITTLEENDIAN16(v);

    memIO->pos += 2;

    return 0;
}

i32 MMemReadI16LE(MMemIO* memIO, i16* val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    i16 v = *((i16*)memIO->pos);
    *(val) = MLITTLEENDIAN16(v);

    memIO->pos += 2;

    return 0;
}

i32 MMemReadU16(MMemIO* memIO, u16* val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *((u16*)memIO->pos);

    memIO->pos += 2;

    return 0;
}

i32 MMemReadU16BE(MMemIO* memIO, u16* val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    u16 v = *((u16*)memIO->pos);
    *(val) = MLITTLEENDIAN16(v);

    memIO->pos += 2;

    return 0;
}

i32 MMemReadU16LE(MMemIO* memIO, u16* val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    u16 v = *((u16*)memIO->pos);
    *(val) = MLITTLEENDIAN16(v);

    memIO->pos += 2;

    return 0;
}

i32 MMemReadI32(MMemIO* memIO, i32* val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *((i32*)memIO->pos);

    memIO->pos += 4;

    return 0;
}

i32 MMemReadI32LE(MMemIO* memIO, i32* val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    i32 v = *((i32*)memIO->pos);
    *(val) = MLITTLEENDIAN32(v);

    memIO->pos += 4;

    return 0;
}

i32 MMemReadI32BE(MMemIO* memIO, i32* val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    i32 v = *((i32*)memIO->pos);
    *(val) = MBIGENDIAN32(v);

    memIO->pos += 4;

    return 0;
}

i32 MMemReadU32(MMemIO* memIO, u32* val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *((u32*)memIO->pos);

    memIO->pos += 4;

    return 0;
}

i32 MMemReadU32LE(MMemIO* memIO, u32* val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    u32 v = *((u32*)memIO->pos);
    *(val) = MLITTLEENDIAN32(v);

    memIO->pos += 4;

    return 0;
}

i32 MMemReadU32BE(MMemIO* memIO, u32* val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    u32 v = *((u32*)memIO->pos);
    *(val) = MBIGENDIAN32(v);

    memIO->pos += 4;

    return 0;
}

i32 MMemReadU8CopyN(MMemIO* memIO, u8* dest, u32 size) {
    return -1;
}

char* MMemReadStr(MMemIO* memIO) {
    u8* oldPos = memIO->pos;
    while (*memIO->pos) { memIO->pos++; }
    return (char*)oldPos;
}

/////////////////////////////////////////////////////////
// Ini file reading

int M_IsWhitespace(char c) {
    return (c == '\n' || c == '\r' || c == '\t' || c == ' ');
}

int M_IsSpaceTab(char c) {
    return (c == '\t' || c == ' ');
}

int M_IsNewLine(char c) {
    return (c == '\n' || c == '\r');
}

const char* MStrEnd(const char* str) {
    if (!str) {
        return NULL;
    }

    while (*str) {
        str++;
    }

    return str;
}

i32 MParseI32(const char* start, const char* end, i32* out) {
    int val = 0;
    int begin = 1;
    int base = 10;

    for (const char* pos = start; pos < end; pos++) {
        char c = *pos;
        if (begin && M_IsWhitespace(c)) {
            continue;
        }

        begin = 0;

        int p = 0;
        if (c >= '0' && c <= '9') {
            p = c - '0';
        } else {
            return MParse_NOT_A_NUMBER;
        }

        val = (val * base) + p;
    }

    *out = val;

    return MParse_SUCCESS;
}

i32 MParseI32Hex(const char* start, const char* end, i32* out) {
    int val = 0;
    int begin = 1;
    int base = 16;

    for (const char* pos = start; pos < end; pos++) {
        char c = *pos;
        if (begin && M_IsWhitespace(c)) {
            continue;
        }

        begin = 0;

        int p = 0;
        if (c >= '0' && c <= '9') {
            p = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            p = 10 + c - 'a';
        } else if (c >= 'A' && c <= 'F') {
            p = 10 + c - 'A';
        } else {
            return MParse_NOT_A_NUMBER;
        }

        val = (val * base) + p;
    }

    *out = val;

    return MParse_SUCCESS;
}

i32 MStrCmp(const char* str1, const char* str2) {
    char c1 = *str1++;
    char c2 = *str2++;
    while (TRUE) {
        if (c1 == 0) {
            if (c2 == 0) {
                return 0;
            } else {
                return -1;
            }
        } else if (c2 == 0) {
            return -1;
        }

        if (c1 != c2) {
            if (c1 > c2) {
                return 1;
            } else {
                return -1;
            }
        }

        c1 = *str1++;
        c2 = *str2++;
    }
}

i32 MStrCmp3(const char* str1, const char* str2Start, const char* str2End) {
    char c1 = *str1, c2;
    for (; str2Start < str2End && c1 != 0; c1 = *str1) {
        c2 = *str2Start;
        if (c1 != c2) {
            if (c1 > c2) {
                return 1;
            } else {
                return -1;
            }
        }
        str1++;
        str2Start++;
    }

    if (c1 == 0 && str2Start == str2End) {
        return 0;
    }

    if (c1 == 0) {
        return -1;
    } else {
        return 1;
    }
}

void MStrU32ToBinary(u32 val, int size, char* out) {
    out[size] = '\0';

    u32 z = (1 << (size - 1));
    for (int i = 0; i < size; i++, z >>= 1) {
        char c = ((val & z) == z) ? '1' : '0';
        out[i] = c;
    }
}

i32 MStringAppend(MMemIO* memIo, const char* str) {
    i32 len = MStrLen(str);

    i32 newSize = memIo->size + len + 1;
    if (newSize > memIo->capacity) {
        M_MemResize(memIo, newSize);
    }

    memcpy(memIo->pos, str, len + 1);
    memIo->size += len;
    memIo->pos += len;

    return len;
}

// -1 is returned in some cases when the size of the buffer is too small,
// Windows only does this when the encoding is incorrect, but GCC will do it
// when the buffer is too small to contain the output.
#define VSNPRINTF_MINUS_1_RETRY 1

i32 MStringAppendf(MMemIO* memIo, const char* format, ...) {
    i32 writableLen = memIo->capacity - memIo->size;
    i32 size = 0;
    va_list vargs;
    va_start(vargs, format);
    if (writableLen <= 1) {
        size = vsnprintf(NULL, 0, format, vargs);
        if (size > 0) {
            i32 newSize = memIo->size + size + 1;
            M_MemResize(memIo, newSize);
            writableLen = memIo->capacity - memIo->size;
            size = vsnprintf((char*)memIo->pos, writableLen, format, vargs);
        }
    } else {
        size = vsnprintf((char*)memIo->pos, writableLen, format, vargs);
        if (size >= writableLen) {
            i32 newSize = memIo->size + size + 1;
            M_MemResize(memIo, newSize);
            writableLen = memIo->capacity - memIo->size;
            size = vsnprintf((char*) memIo->pos, writableLen, format, vargs);
        }
#if VSNPRINTF_MINUS_1_RETRY == 1
        else if (size < 0) {
            size = vsnprintf(NULL, 0, format, vargs);
            if (size > 0) {
                i32 newSize = memIo->size + size + 1;
                M_MemResize(memIo, newSize);
                writableLen = memIo->capacity - memIo->size;
                size = vsnprintf((char*)memIo->pos, writableLen, format, vargs);
            }
        }
#endif
    }

    if (size < 0) {
        MLogf("Encoding error for sprintf format: %s", format);
    } else {
        memIo->size += size;
        memIo->pos += size;
    }

    va_end(vargs);

    return size;
}

i32 MIniLoadFile(MIni* ini, const char* fileName) {
    MReadFileRet ret = MFileReadFully(fileName);
    if (ret.size) {
        ini->data = ret.data;
        ini->dataSize = ret.size;
        ini->owned = 1;
        return MIniParse(ini);
    }
    return 0;
}

enum MIniParseState {
    MIniParseState_INIT,
    MIniParseState_READ_KEY,
    MIniParseState_READ_EQUALS,
    MIniParseState_READ_EQUALS_AFTER,
    MIniParseState_IGNORE_LINE,
    MIniParseState_READ_VALUE
};

i32 MIniParse(MIni* ini) {
    MArrayInit(ini->values);

    MIniPair* pair;

    enum MIniParseState state = MIniParseState_INIT;

    char* startKey = 0;
    char* endKey = 0;
    char* startValue = 0;
    char* pos = (char*)ini->data;

    for (int i = 0; i < ini->dataSize; i++) {
        char c = *pos;
        switch (state) {
            case MIniParseState_INIT:
                if (!M_IsWhitespace(c)) {
                    startKey = pos;
                    state = MIniParseState_READ_KEY;
                }
                break;
            case MIniParseState_READ_KEY:
                if (M_IsSpaceTab(c)) {
                    endKey = pos;
                    state = MIniParseState_READ_EQUALS;
                } else if (M_IsNewLine(c)) {
                    state = MIniParseState_INIT;
                } else if (c == '=') {
                    endKey = pos;
                    state = MIniParseState_READ_EQUALS_AFTER;
                }
                break;
            case MIniParseState_READ_EQUALS:
                if (c == '=') {
                    state = MIniParseState_READ_EQUALS_AFTER;
                } else if (!M_IsSpaceTab(c)) {
                    state = MIniParseState_IGNORE_LINE;
                }
                break;
            case MIniParseState_IGNORE_LINE:
                if (M_IsNewLine(c)) {
                    state = MIniParseState_INIT;
                }
                break;
            case MIniParseState_READ_EQUALS_AFTER:
                if (M_IsNewLine(c)) {
                    state = MIniParseState_INIT;
                } else if (!M_IsSpaceTab(c)) {
                    startValue = pos;
                    state = MIniParseState_READ_VALUE;
                }
                break;
            case MIniParseState_READ_VALUE:
                if (M_IsWhitespace(c)) {
                    pair = MArrayAddPtr(ini->values);
                    pair->key = startKey;
                    pair->keySize = endKey - startKey;
                    pair->val = startValue;
                    pair->valSize = pos - startValue;
                    state = MIniParseState_INIT;
                }
                break;
        }
        pos++;
    }

    return 1;
}

i32 MIniFree(MIni* ini) {
    MArrayFree(ini->values);

    if (ini->owned) {
        MFree(ini->data); ini->data = 0;
        ini->owned = 0;
    }

    return -1;
}

i32 MIniReadI32(MIni* ini, const char* key, i32* valOut) {
    u32 inSize = strlen(key);

    for (int i = 0; i < MArraySize(ini->values); i++) {
        MIniPair* pair = MArrayGetPtr(ini->values, i);

        if (inSize == pair->keySize) {
            int notEqual = 0;
            for (int j = 0; j < pair->keySize; j++) {
                if (pair->key[j] != key[j]) {
                    notEqual = 1;
                    break;
                }
            }

            if (!notEqual) {
                return MParseI32(pair->val, pair->val + pair->valSize, valOut);
            }
        }
    }

    return -1;
}

i32 MIniSaveFile(MIniSave* ini, const char* filePath) {
    ini->fileHandle = 0;

#ifdef USE_SDL
    SDL_RWops *file = SDL_RWFromFile(filePath, "w");
    if (file == NULL) {
        return -1;
    }

    ini->fileHandle = file;
#endif

    return 0;
}

i32 MIniSaveMWriteI32(MIniSave* ini, const char* key, i32 value) {
#ifdef USE_SDL
    SDL_RWops *file = (SDL_RWops*)ini->fileHandle;

    const int bufferSize = 1024;
    char buffer[bufferSize];

    int n = snprintf(buffer, bufferSize, "%s = %d\n", key, value);
    SDL_RWwrite(file, buffer, n, 1);
#endif

    return 0;
}

void MIniSaveFree(MIniSave* ini) {
    if (ini->fileHandle) {
#ifdef USE_SDL
        SDL_RWclose((SDL_RWops*)ini->fileHandle);
#endif
    }
    ini->fileHandle = 0;
}

/////////////////////////////////////////////////////////
// Files

MReadFileRet MFileReadFullyWithOffset(const char* filePath, u32 maxSize, u32 offset) {
    MReadFileRet ret;
    ret.size = 0;
    ret.data = NULL;

#ifdef USE_SDL
    SDL_RWops *file = SDL_RWFromFile(filePath, "rb");
    if (file == NULL) {
        return ret;
    }

    u32 size = maxSize;
    if (!maxSize) {
        size = SDL_RWsize(file);
    }

    if (size > 30 * 1024 * 1024) {
        return ret;
    }

    ret.data = (u8*)MMalloc(size);

    if (offset) {
        SDL_RWseek(file, offset, RW_SEEK_SET);
    }

    size_t bytesRead = SDL_RWread(file, ret.data, 1, size);
    ret.size = bytesRead;

    SDL_RWclose(file);
#else
    FILE *file = fopen(filePath, "rb");
    if (file == NULL) {
        return ret;
    }

    u32 size = maxSize;
    if (!maxSize) {
        fseek(file, 0L, SEEK_END);
        size = ftell(file);
        fseek(file, 0L, SEEK_SET);
    }

    if (size > 30 * 1024 * 1024) {
        return ret;
    }

    ret.data = (u8*)MMalloc(size);

    if (offset) {
        fseek(file, offset, SEEK_SET);
    }

    size_t bytesRead = fread(ret.data, size, 1, file);

    ret.size = bytesRead;

    fclose(file);
#endif

    return ret;
}

MFileInfo MGetFileInfo(const char* filePath) {
    MFileInfo fileInfo;
    memset(&fileInfo, 0, sizeof(fileInfo));
#ifdef AMIGA
#else
#ifdef __APPLE__
    struct stat status;
    if (stat(filePath, &status) == -1) {
#else
    struct _stat64 status;
    if (_stat64(filePath, &status) == -1) {
#endif
        return fileInfo;
    }
    fileInfo.exists = TRUE;
    fileInfo.lastModifiedTime = status.st_mtime;
    fileInfo.size = status.st_size;
#endif
    return fileInfo;
}

i32 MFileWriteDataFully(const char* filePath, u8* data, u32 size) {
#ifdef USE_SDL
    SDL_RWops *file = SDL_RWFromFile(filePath, "wb");
    if (file == NULL) {
        return 0;
    }

    SDL_RWwrite(file, data, size, 1);
    SDL_RWclose(file);
#endif
    return 0;
}

MFile MFileWriteOpen(const char* filePath) {
    MFile fileData;
    fileData.handle = fopen(filePath, "wb");
    if (fileData.handle == NULL) {
        fileData.open = 0;
        return fileData;
    }
    fileData.open = 1;
    return fileData;
}

i32 MFileWriteData(MFile* file, u8* data, u32 size) {
    if (!file->open) {
        return -1;
    }

    return fwrite(data, size, 1, (FILE*)file->handle);
}

void MFileClose(MFile* file) {
    if (file->open) {
        fclose((FILE*)file->handle);
        file->open = 0;
    }
}
