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

// Define to log allocations
// #define M_LOG_ALLOCATIONS

static MAllocator* sAllocator;

#ifndef M_CLIB_DISABLE

#ifdef M_CLIB_USE_ALIGNED
#define M_ALLOC_ALIGNMENT 64

void* default_malloc(void* alloc, size_t size) {
    return _aligned_malloc(size, M_ALLOC_ALIGNMENT);
}

void* default_realloc(void* alloc, void* mem, size_t oldSize, size_t newSize) {
    return _aligned_realloc(mem, newSize, M_ALLOC_ALIGNMENT);
}

void default_free(void* alloc, void* mem, size_t size) {
    _aligned_free(mem);
}
#else
void* default_malloc(void* alloc, size_t size) {
    return malloc(size);
}

void* default_realloc(void* alloc, void* mem, size_t oldSize, size_t newSize) {
    return realloc(mem, newSize);
}

void default_free(void* alloc, void* mem, size_t size) {
    free(mem);
}
#endif

static MAllocator sClibMAllocator = {
    default_malloc,
    default_realloc,
    default_free
};

void MMemUseClibAllocator() {
    sAllocator = &sClibMAllocator;
}

#endif

void* MBumpMalloc(void* _ba, size_t size) {
    MMemBumpAllocator* ba = _ba;

    u8* pos = ba->end;
    ba->end += size;

    size_t memUsed = ba->end - ba->mem;
    if (ba->size < memUsed) {
        MLogf("malloc(%d) failed: out of memory - using %d of %d", size, memUsed, ba->size);
    }

    return pos;
}

void* MBumpRealloc(void* _ba, void* mem, size_t oldSize, size_t newSize) {
    MMemBumpAllocator* ba = _ba;

    u8* pos = ba->end;
    memcpy(pos, mem, oldSize);
    ba->end += newSize;

    size_t memUsed = ba->end - ba->mem;
    if (ba->size < memUsed) {
        MLogf("realloc(%d) failed: out of memory - using %d of %d", newSize, memUsed, ba->size);
    }

    return pos;
}

void MBumpFree(void* _ba, void* mem, size_t size) {
    // nop
}

MMemBumpAllocator* MMemBumpAllocInit(u8* mem, size_t size) {
    MMemBumpAllocator ba;
    ba.mem = mem;
    ba.end = mem;
    ba.size = size;
    ba.alloc.mallocFunc = MBumpMalloc;
    ba.alloc.reallocFunc = MBumpRealloc;
    ba.alloc.freeFunc = MBumpFree;

    MMemBumpAllocator* b = (MMemBumpAllocator*)MBumpMalloc(&ba, sizeof(MMemBumpAllocator));
    *b = ba;
    return b;
}

void MMemBumpDumpInfo(MMemBumpAllocator* ba) {
    size_t size_used = ba->end - ba->mem;
    MLogf("Bump allocator used: %d (0x%x) bytes", size_used, size_used);
}

void MMemAllocSet(MAllocator* allocator) {
    sAllocator = allocator;
}

#ifdef M_MEM_DEBUG

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

MARRAY_TYPEDEF(MMemAllocInfo, MMemAllocationSlots)
MARRAY_TYPEDEF(u32, MMemAllocationFreeSlots)

typedef struct sMMemDebugContext {
    MMemAllocationSlots allocSlots;
    MMemAllocationFreeSlots freeSlots;
    b32 sMemDebugInitialized;
    u32 totalAllocations;
    u32 totalAllocatedBytes;
    u32 curAllocatedBytes;
    u32 maxAllocatedBytes;
} MMemDebugContext;

static MMemDebugContext sMemDebugContext;

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

        void* mem = sAllocator->reallocFunc(sAllocator, *arr, itemSize *capacity, itemSize * newCapacity);
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
    if (!sMemDebugContext.sMemDebugInitialized) {
        MArrayInit(sMemDebugContext.allocSlots);
        MArrayInit(sMemDebugContext.freeSlots);
        sMemDebugContext.sMemDebugInitialized = TRUE;
    }
}

void MMemDebugDeinit() {
    if (!sMemDebugContext.sMemDebugInitialized) {
        return;
    }

    MLogf("Total allocations: %ld", sMemDebugContext.totalAllocations);
    MLogf("Total allocated: %ld bytes", sMemDebugContext.totalAllocatedBytes);
    MLogf("Max memory used: %ld bytes", sMemDebugContext.maxAllocatedBytes);

    MMemDebugCheckAll();

    for (u32 i = 0; i < MArraySize(sMemDebugContext.allocSlots); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemDebugContext.allocSlots, i);
        if (memAlloc->start) {
            MLogf("Leaking allocation: %s:%d 0x%p (%d)", memAlloc->file, memAlloc->line, memAlloc->start,
                  memAlloc->size);
        }
    }

    sAllocator->freeFunc(sAllocator, sMemDebugContext.allocSlots.arr,
                         sMemDebugContext.allocSlots.p.capacity * sizeof(MMemAllocInfo));
    sMemDebugContext.allocSlots.arr = NULL;
    sMemDebugContext.allocSlots.p.capacity = 0;
    sMemDebugContext.allocSlots.p.size = 0;

    sAllocator->freeFunc(sAllocator, sMemDebugContext.freeSlots.arr,
                         sMemDebugContext.freeSlots.p.capacity * sizeof(u32));
    sMemDebugContext.freeSlots.arr = NULL;
    sMemDebugContext.freeSlots.p.capacity = 0;
    sMemDebugContext.freeSlots.p.size = 0;

    sMemDebugContext.sMemDebugInitialized = FALSE;
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

static void LogBytesOverwritten(u8* startSentintel, u8* startOverwrite, u32 bytesOverwritten, b32 isAfter) {
    if (isAfter) {
        MLogf("offset: %d bytes: %d", startOverwrite - startSentintel, bytesOverwritten);
    } else {
        MLogf("offset: %d bytes: %d", startOverwrite - (SENTINEL_BEFORE + startSentintel), bytesOverwritten);
    }

    MLogBytes(startOverwrite, bytesOverwritten);
}

void MMemDebugSentinelCheckMLog(void* sentinelMemStart, size_t size, b32 isAfter) {
    u8* sentinelStart = (u8*)sentinelMemStart;
    u32 bytesOverwritten = 0;

    int state = 0;
    u8* overwriteStart = 0;
    for (u32 i = 0; i < size; ++i) {
        if (sentinelStart[i] != sMagicSentinelValues[i & 0x3]) {
            if (state == 0) {
                overwriteStart = sentinelStart + i;
                state = 1;
                bytesOverwritten = 0;
            }
            bytesOverwritten++;
        } else if (state == 1) {
            LogBytesOverwritten(sentinelStart, overwriteStart, bytesOverwritten, isAfter);
            state = 0;
        }
    }

    if (state == 1) {
        LogBytesOverwritten(sentinelStart, overwriteStart, bytesOverwritten, isAfter);
    }
}

b32 MMemDebugCheckMemAlloc(MMemAllocInfo* memAlloc) {
    if (!memAlloc->mem) {
        return FALSE;
    }

    u32 beforeBytes = MMemDebugSentinelCheck(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    u32 afterBytes = MMemDebugSentinelCheck(memAlloc->mem + memAlloc->size, SENTINEL_AFTER);
    if (beforeBytes) {
        MLogf("MMemDebugCheck: %s:%d : %d bytes overwritten before:", memAlloc->file, memAlloc->line, beforeBytes);
        MMemDebugSentinelCheckMLog(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE, FALSE);
        MMemDebugSentinelSet(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    }

    if (afterBytes) {
        MLogf("MMemDebugCheck: %s:%d : %d bytes overwritten after:", memAlloc->file, memAlloc->line, afterBytes);
        MMemDebugSentinelCheckMLog(memAlloc->mem + memAlloc->size, SENTINEL_AFTER, TRUE);
        MLogBytes(memAlloc->mem + SENTINEL_BEFORE, 20);
        MMemDebugSentinelSet(memAlloc->mem + memAlloc->size, SENTINEL_AFTER);
    }

    return !(afterBytes | beforeBytes);
}

b32 MMemDebugCheck(void* p) {
    MMemAllocInfo* memAllocFound = NULL;
    for (u32 i = 0; i < MArraySize(sMemDebugContext.allocSlots); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemDebugContext.allocSlots, i);
        if (p == memAlloc->mem) {
            memAllocFound = memAlloc;
            break;
        }
    }

    if (memAllocFound == NULL) {
        MLogf("MMemDebugCheck called on invalid ptr %p", p);
        return FALSE;
    }

    return MMemDebugCheckMemAlloc(memAllocFound);
}

b32 MMemDebugCheckAll() {
    b32 memOk = TRUE;
    for (u32 i = 0; i < MArraySize(sMemDebugContext.allocSlots); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemDebugContext.allocSlots, i);
        if (!MMemDebugCheckMemAlloc(memAlloc)) {
            memOk = FALSE;
        }
    }

    return memOk;
}

#endif

void* _MMalloc(MDEBUG_SOURCE_DEFINE size_t size) {
#ifndef M_CLIB_DISABLE
    if (!sAllocator) {
        sAllocator = &sClibMAllocator;
    }
#endif

#ifdef M_MEM_DEBUG
#ifdef M_LOG_ALLOCATIONS
    MLogf("malloc %s:%d %d", file, line, size);
#endif

    if (!sMemDebugContext.sMemDebugInitialized) {
        MMemDebugInit();
        atexit(MMemDebugDeinit);
    }

    MMemAllocInfo* memAlloc = NULL;
    int pos = 0;
    if (MArraySize(sMemDebugContext.freeSlots)) {
        pos = MArrayPop(sMemDebugContext.freeSlots);
        memAlloc = MArrayGetPtr(sMemDebugContext.allocSlots, pos);
    } else {
        memAlloc = MMemDebugArrayAddPtr(sMemDebugContext.allocSlots);
        pos = sMemDebugContext.allocSlots.p.size - 1;
    }

    size_t start = SENTINEL_BEFORE;
    size_t allocSize = start + size + SENTINEL_AFTER;
    memAlloc->size = size;
    memAlloc->start = (u8*)sAllocator->mallocFunc(sAllocator, allocSize);
    memAlloc->mem = (u8*) memAlloc->start + SENTINEL_BEFORE;
    memAlloc->file = file;
    memAlloc->line = line;

    sMemDebugContext.totalAllocations += 1;
    sMemDebugContext.totalAllocatedBytes += size;
    sMemDebugContext.curAllocatedBytes += size;
    if (sMemDebugContext.curAllocatedBytes > sMemDebugContext.maxAllocatedBytes) {
        sMemDebugContext.maxAllocatedBytes = sMemDebugContext.curAllocatedBytes;
    }

    MMemDebugSentinelSet(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    MMemDebugSentinelSet(memAlloc->mem + memAlloc->size, SENTINEL_AFTER);

#ifdef M_LOG_ALLOCATIONS
    MLogf("-> 0x%p", memAlloc->mem);
#endif
    return memAlloc->mem;
#else
    return sAllocator->mallocFunc(sAllocator, size);
#endif
}

void _MFree(MDEBUG_SOURCE_DEFINE void* p, size_t size) {
#ifndef M_CLIB_DISABLE
    if (!sAllocator) {
        sAllocator = &sClibMAllocator;
    }
#endif
    if (p == NULL) {
        return;
    }

#ifdef M_MEM_DEBUG
    for (u32 i = 0; i < MArraySize(sMemDebugContext.allocSlots); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemDebugContext.allocSlots, i);
        if (p == memAlloc->mem) {
            MMemDebugCheckMemAlloc(memAlloc);
            if (size != memAlloc->size) {
                MLogf("MFree %s:%d 0x%p called with size: %d - should be: %d", file, line, p, size,
                      memAlloc->size);
            }
            sAllocator->freeFunc(sAllocator, memAlloc->start, SENTINEL_BEFORE + memAlloc->size + SENTINEL_AFTER);
            sMemDebugContext.curAllocatedBytes -= memAlloc->size;
            memAlloc->start = 0;
            memAlloc->mem = 0;
#ifdef M_LOG_ALLOCATIONS
            MLogf("free %s:%d 0x%p %d   allocated @ %s:%d" , file, line, p, memAlloc->size,
                  memAlloc->file, memAlloc->line);
#endif
            *(MMemDebugArrayAddPtr(sMemDebugContext.freeSlots)) = i;
            return;
        }
    }

    MLogf("MFree %s:%d called on invalid ptr: 0x%p", file, line, p);
#else
    sAllocator->freeFunc(sAllocator, p, size);
#endif
}

void* _MRealloc(MDEBUG_SOURCE_DEFINE void* p, size_t oldSize, size_t newSize) {
#ifndef M_CLIB_DISABLE
    if (!sAllocator) {
        sAllocator = &sClibMAllocator;
    }
#endif

#ifdef M_MEM_DEBUG
#ifdef M_LOG_ALLOCATIONS
    MLogf("realloc %s:%d %d", file, line, newSize);
#endif
    if (p == NULL) {
        return _MMalloc(MDEBUG_SOURCE_PASS newSize);
    }

    MMemAllocInfo* memAllocFound = NULL;
    for (u32 i = 0; i < MArraySize(sMemDebugContext.allocSlots); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemDebugContext.allocSlots, i);
        if (p == memAlloc->mem) {
            MMemDebugCheckMemAlloc(memAlloc);
            memAllocFound = memAlloc;
            break;
        }
    }

    if (memAllocFound == NULL) {
        MLogf("MRealloc called on invalid ptr at line: %s:%d %p", file, line, p);
        return p;
    }

    if (memAllocFound->size != oldSize) {
        MLogf("MRealloc %s:%d 0x%p called with old size: %d - should be: %d", file, line, p, oldSize,
              memAllocFound->size);
    }

    sMemDebugContext.totalAllocations += 1;
    sMemDebugContext.totalAllocatedBytes += newSize;
    sMemDebugContext.curAllocatedBytes += newSize - memAllocFound->size;
    if (sMemDebugContext.curAllocatedBytes > sMemDebugContext.maxAllocatedBytes) {
        sMemDebugContext.maxAllocatedBytes = sMemDebugContext.curAllocatedBytes;
    }

    size_t allocSize = SENTINEL_BEFORE + newSize + SENTINEL_AFTER;
    memAllocFound->size = newSize;
    memAllocFound->start = (u8*)sAllocator->reallocFunc(sAllocator, memAllocFound->start,
                                                        SENTINEL_BEFORE + oldSize + SENTINEL_AFTER, allocSize);
    memAllocFound->mem = memAllocFound->start + SENTINEL_BEFORE;
    memAllocFound->file = file;
    memAllocFound->line = line;

    MMemDebugSentinelSet( memAllocFound->start, SENTINEL_BEFORE);
    MMemDebugSentinelSet(memAllocFound->mem + memAllocFound->size, SENTINEL_AFTER);

#ifdef M_LOG_ALLOCATIONS
    MLogf("-> %p (resized)", memAllocFound->mem);
#endif
    return memAllocFound->mem;
#else
    return sAllocator->reallocFunc(sAllocator, p, oldSize, newSize);
#endif
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

static const char* sHexChars = "0123456789abcdef";

void MLogBytes(const u8* mem, u32 len) {
    const int bytesPerLine = 64; // must be power of two - see AND (&) below
    char buff[bytesPerLine + 1];
    int buffPos = 0;

    for (int j  = 0; j < len; ++j) {
        u8 val = mem[j];
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

/////////////////////////////////////////////////////////
// Dynamic Array

i32 M_ArrayGrow(MDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize, u32 minNeeded) {
    u32 capacity = p->capacity;
    u32 newCapacity = (capacity > 4) ? (2 * capacity) : 8; // allocate for at least 8 elements

    if (minNeeded > newCapacity) {
        newCapacity = minNeeded;
    }

    void* mem = _MRealloc(MDEBUG_SOURCE_PASS *arr, itemSize * capacity, itemSize * newCapacity);
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

void M_ArrayFree(MDEBUG_SOURCE_DEFINE void** arr, MArrayInfo* p, u32 itemSize) {
    _MFree(MDEBUG_SOURCE_PASS *arr, itemSize * p->capacity); *arr = NULL;
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
    memIO->mem = MRealloc(memIO->mem, memIO->capacity, newSize);
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

    u8 a[] = {val >> 8, val & 0xff };
    memcpy(memIO->pos, a, 2);
    memIO->pos += 2;
    memIO->size = newSize;
}

void MMemWriteU16LE(MMemIO* memIO, u16 val) {
    u32 newSize = memIO->size + 2;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8 a[] = {val & 0xff, val >> 8 };
    memcpy(memIO->pos, a, 2);
    memIO->pos += 2;
    memIO->size = newSize;
}

void MMemWriteU32BE(MMemIO* memIO, u32 val) {
    u32 newSize = memIO->size + 4;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8 a[] = {val >> 24, (val >> 16) & 0xff, (val >> 8) & 0xff, (val & 0xff) };
    memcpy(memIO->pos, a, 4);
    memIO->pos += 4;
    memIO->size = newSize;
}

void MMemWriteU32LE(MMemIO* memIO, u32 val) {
    u32 newSize = memIO->size + 4;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8 a[] = { (val & 0xff), (val >> 8) & 0xff, (val >> 16) & 0xff, (val >> 24) };
    memcpy(memIO->pos, a, 4);
    memIO->pos += 4;
    memIO->size = newSize;
}

void MMemWriteU8CopyN(MMemIO*restrict memIO, u8*restrict src, u32 size) {
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

void MMemWriteI8CopyN(MMemIO*restrict memIO, i8*restrict src, u32 size) {
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

i32 MMemReadI8(MMemIO*restrict memIO, i8*restrict val) {
    if (memIO->pos >= memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *((i8*)(memIO->pos++));

    return 0;
}

i32 MMemReadU8(MMemIO*restrict memIO, u8*restrict val) {
    if (memIO->pos >= memIO->mem + memIO->size) {
        return -1;
    }

    *(val) = *(memIO->pos++);

    return 0;
}

i32 MMemReadI16(MMemIO*restrict memIO, i16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    memcpy(val, memIO->pos, 2);
    memIO->pos += 2;
    return 0;
}

i32 MMemReadI16BE(MMemIO*restrict memIO, i16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MLITTLEENDIAN
    i16 v;
    memcpy(&v, memIO->pos, 2);
    *(val) = MBIGENDIAN16(v);
#else
    memcpy(val, memIO->pos, 2);
#endif
    memIO->pos += 2;
    return 0;
}

i32 MMemReadI16LE(MMemIO*restrict memIO, i16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MBIGENDIAN
    i16 v;
    memcpy(&v, memIO->pos, 2);
    *(val) = MLITTLEENDIAN16(v);
#else
    memcpy(val, memIO->pos, 2);
#endif
    memIO->pos += 2;
    return 0;
}

i32 MMemReadU16(MMemIO*restrict memIO, u16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    memcpy(val, memIO->pos, 2);
    memIO->pos += 2;
    return 0;
}

i32 MMemReadU16BE(MMemIO*restrict memIO, u16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MLITTLEENDIAN
    u16 v;
    memcpy(&v, memIO->pos, 2);
    *(val) = MBIGENDIAN16(v);
#else
    memcpy(val, memIO->pos, 2);
#endif
    memIO->pos += 2;
    return 0;
}

i32 MMemReadU16LE(MMemIO*restrict memIO, u16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MBIGENDIAN
    u16 v;
    memcpy(&v, memIO->pos, 2);
    *(val) = MLITTLEENDIAN16(v);
#else
    memcpy(val, memIO->pos, 2);
#endif
    memIO->pos += 2;
    return 0;
}

i32 MMemReadI32(MMemIO*restrict memIO, i32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    memcpy(val, memIO->pos, 4);
    memIO->pos += 4;
    return 0;
}

i32 MMemReadI32LE(MMemIO*restrict memIO, i32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MBIGENDIAN
    i32 v;
    memcpy(&v, memIO->pos, 4);
    *(val) = MLITTLEENDIAN32(v);
#else
    memcpy(val, memIO->pos, 4);
#endif
    memIO->pos += 4;
    return 0;
}

i32 MMemReadI32BE(MMemIO*restrict memIO, i32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MLITTLEENDIAN
    i32 v;
    memcpy(&v, memIO->pos, 4);
    *(val) = MBIGENDIAN32(v);
#else
    memcpy(val, memIO->pos, 4);
#endif
    memIO->pos += 4;
    return 0;
}

i32 MMemReadU32(MMemIO*restrict memIO, u32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    memcpy(val, memIO->pos, 4);
    memIO->pos += 4;
    return 0;
}

i32 MMemReadU32LE(MMemIO*restrict memIO, u32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MBIGENDIAN
    i32 v;
    memcpy(&v, memIO->pos, 4);
    *(val) = MLITTLEENDIAN32(v);
#else
    memcpy(val, memIO->pos, 4);
#endif
    memIO->pos += 4;
    return 0;
}

i32 MMemReadU32BE(MMemIO*restrict memIO, u32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MLITTLEENDIAN
    i32 v;
    memcpy(&v, memIO->pos, 4);
    *(val) = MBIGENDIAN32(v);
#else
    memcpy(val, memIO->pos, 4);
#endif
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
    va_list vargs1;

    va_start(vargs1, format);
    va_list vargs2;
    va_copy(vargs2, vargs1);
    if (writableLen <= 1) {
        size = vsnprintf(NULL, 0, format, vargs1);
        if (size > 0) {
            i32 newSize = memIo->size + size + 1;
            M_MemResize(memIo, newSize);
            writableLen = memIo->capacity - memIo->size;
            size = vsnprintf((char*)memIo->pos, writableLen, format, vargs2);
        }
    } else {
        size = vsnprintf((char*)memIo->pos, writableLen, format, vargs1);
        if (size >= writableLen) {
            i32 newSize = memIo->size + size + 1;
            M_MemResize(memIo, newSize);
            writableLen = memIo->capacity - memIo->size;
            size = vsnprintf((char*) memIo->pos, writableLen, format, vargs2);
        }
#if VSNPRINTF_MINUS_1_RETRY == 1
        else if (size < 0) {
            size = vsnprintf(NULL, 0, format, vargs1);
            if (size > 0) {
                i32 newSize = memIo->size + size + 1;
                M_MemResize(memIo, newSize);
                writableLen = memIo->capacity - memIo->size;
                size = vsnprintf((char*)memIo->pos, writableLen, format, vargs2);
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

    va_end(vargs1);
    va_end(vargs2);

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

i32 _MIniFree(MDEBUG_SOURCE_DEFINE MIni* ini) {
    MArrayFree(ini->values);

    if (ini->owned) {
        _MFree(MDEBUG_SOURCE_PASS ini->data, ini->dataSize); ini->data = 0;
        ini->owned = 0;
    }

    return -1;
}

i32 MIniReadI32(MIni* ini, const char* key, i32* valOut) {
    u32 inSize = MStrLen(key);

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

    size_t bytesRead = fread(ret.data, 1, size, file);

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
#if defined(__APPLE__) || defined(__EMSCRIPTEN__)
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

    return fwrite(data, 1, size, (FILE*)file->handle);
}

void MFileClose(MFile* file) {
    if (file->open) {
        fclose((FILE*)file->handle);
        file->open = 0;
    }
}
