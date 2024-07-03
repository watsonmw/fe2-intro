#include "mlib.h"

#ifndef M_CLIB_DISABLE
#include <stdlib.h>
#endif

// Define to log allocations
// #define M_LOG_ALLOCATIONS

static MAllocator* sAllocator;

#ifndef M_CLIB_DISABLE

void* default_malloc(void* alloc, size_t size) {
    return malloc(size);
}

void* default_realloc(void* alloc, void* mem, size_t oldSize, size_t newSize) {
    return realloc(mem, newSize);
}

void default_free(void* alloc, void* mem, size_t size) {
    free(mem);
}

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

    size = MSizeAlign(size, ba->align);
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
    newSize = MSizeAlign(newSize, ba->align);

    memmove(pos, mem, oldSize);
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

MMemBumpAllocator* MMemBumpAllocInit(u8* mem, size_t size, size_t alignBytes) {
    MMemBumpAllocator ba;
    ba.mem = mem;
    ba.end = mem;
    ba.size = size;
    ba.align = alignBytes;
    ba.alloc.mallocFunc = MBumpMalloc;
    ba.alloc.reallocFunc = MBumpRealloc;
    ba.alloc.freeFunc = MBumpFree;

    MMemBumpAllocator* b = (MMemBumpAllocator*)MBumpMalloc(&ba, sizeof(MMemBumpAllocator));
    *b = ba;

    b->end = MPtrAlign(b->end, ba.align);
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

MINLINE i32 MMemDebug_ArrayGrow1(void** arr, MArrayInfo* p, u32 itemSize) {
    u32 minNeeded = p->size + 1;
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
            MLogf("MMemDebug_ArrayGrowIfNeeded realloc failed for %x", *arr);
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

#define MMemDebugArrayAddPtr(a) (MMemDebug_ArrayGrow1(M_ArrayUnpack(a)) ? (((a).arr + (a).p.size++)) : 0)

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

    if (beforeBytes || afterBytes) {
        MLogf("MMemDebugCheck: %s:%d : 0x%p [%d]", memAlloc->file, memAlloc->line, memAlloc->mem,
              memAlloc->size);
    }

    if (beforeBytes) {
        MLogf("    %d bytes overwritten before:", beforeBytes);
        MMemDebugSentinelCheckMLog(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE, FALSE);
        MMemDebugSentinelSet(memAlloc->mem - SENTINEL_BEFORE, SENTINEL_BEFORE);
    }

    if (afterBytes) {
        MLogf("    %d bytes overwritten after:", afterBytes);
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

b32 MMemDebugListAll() {
    b32 memOk = TRUE;
    for (u32 i = 0; i < MArraySize(sMemDebugContext.allocSlots); ++i) {
        MMemAllocInfo* memAlloc = MArrayGetPtr(sMemDebugContext.allocSlots, i);
        MLogf("%s:%d %p %d", memAlloc->file, memAlloc->line, memAlloc->mem, memAlloc->size);
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
#ifndef M_CLIB_DISABLE
        atexit(MMemDebugDeinit);
#endif
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
    memAlloc->mem = (u8*)memAlloc->start + SENTINEL_BEFORE;
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
#ifdef M_LOG_ALLOCATIONS
    MLogf("malloc %d", size);
    void* mem = sAllocator->mallocFunc(sAllocator, size);
    MLogf("-> 0x%p", mem);
    return mem;
#else
    return sAllocator->mallocFunc(sAllocator, size);
#endif
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
            MLogf("free %s:%d 0x%p %d   [ allocated @ %s:%d ]", file, line, p, memAlloc->size,
                  memAlloc->file, memAlloc->line);
#endif
            *(MMemDebugArrayAddPtr(sMemDebugContext.freeSlots)) = i;
            return;
        }
    }

    MLogf("MFree %s:%d called on invalid ptr: 0x%p", file, line, p);
#else
#ifdef M_LOG_ALLOCATIONS
    MLogf("free 0x%p", p);
#endif
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
    MLogf("realloc %s:%d 0x%p %d %d", file, line, p, oldSize, newSize);
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
#ifdef M_LOG_ALLOCATIONS
    MLogf("realloc 0x%p %d", p, newSize);
    void* mem = sAllocator->reallocFunc(sAllocator, p, oldSize, newSize);
    MLogf("-> 0x%p (resized)", mem);
    return mem;
#else
    return sAllocator->reallocFunc(sAllocator, p, oldSize, newSize);
#endif
#endif
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
        MLogf("M_ArrayGrow realloc failed for %x", *arr);
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

void MMemGrowBytes(MMemIO* memIO, u32 capacity) {
    i32 newSize = memIO->size + capacity;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }
}

u8* MMemAddBytes(MMemIO* memIO, u32 size) {
    i32 newSize = memIO->size + size;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8* start = memIO->pos;
    memIO->pos += size;
    memIO->size = newSize;
    return start;
}

u8* MMemAddBytesZero(MMemIO* memIO, u32 size) {
    u32 newSize = memIO->size + size;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8* start = memIO->pos;
    memset(memIO->pos, 0, size);
    memIO->pos += size;
    memIO->size = newSize;
    return start;
}

void MMemWriteU16BE(MMemIO* memIO, u16 val) {
    u32 newSize = memIO->size + 2;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8 a[] = {val >> 8, val & 0xff };
    memmove(memIO->pos, a, 2);
    memIO->pos += 2;
    memIO->size = newSize;
}

void MMemWriteU16LE(MMemIO* memIO, u16 val) {
    u32 newSize = memIO->size + 2;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8 a[] = {val & 0xff, val >> 8 };
    memmove(memIO->pos, a, 2);
    memIO->pos += 2;
    memIO->size = newSize;
}

void MMemWriteU32BE(MMemIO* memIO, u32 val) {
    u32 newSize = memIO->size + 4;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8 a[] = {val >> 24, (val >> 16) & 0xff, (val >> 8) & 0xff, (val & 0xff) };
    memmove(memIO->pos, a, 4);
    memIO->pos += 4;
    memIO->size = newSize;
}

void MMemWriteU32LE(MMemIO* memIO, u32 val) {
    u32 newSize = memIO->size + 4;
    if (newSize > memIO->capacity) {
        M_MemResize(memIO, newSize);
    }

    u8 a[] = { (val & 0xff), (val >> 8) & 0xff, (val >> 16) & 0xff, (val >> 24) };
    memmove(memIO->pos, a, 4);
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

    memmove(memIO->pos, src, size);

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

    memmove(memIO->pos, src, size);

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

    memmove(val, memIO->pos, 2);
    memIO->pos += 2;
    return 0;
}

i32 MMemReadI16BE(MMemIO*restrict memIO, i16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MLITTLEENDIAN
    i16 v;
    memmove(&v, memIO->pos, 2);
    *(val) = MBIGENDIAN16(v);
#else
    memmove(val, memIO->pos, 2);
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
    memmove(&v, memIO->pos, 2);
    *(val) = MLITTLEENDIAN16(v);
#else
    memmove(val, memIO->pos, 2);
#endif
    memIO->pos += 2;
    return 0;
}

i32 MMemReadU16(MMemIO*restrict memIO, u16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

    memmove(val, memIO->pos, 2);
    memIO->pos += 2;
    return 0;
}

i32 MMemReadU16BE(MMemIO*restrict memIO, u16*restrict val) {
    if (memIO->pos + 2 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MLITTLEENDIAN
    u16 v;
    memmove(&v, memIO->pos, 2);
    *(val) = MBIGENDIAN16(v);
#else
    memmove(val, memIO->pos, 2);
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
    memmove(&v, memIO->pos, 2);
    *(val) = MLITTLEENDIAN16(v);
#else
    memmove(val, memIO->pos, 2);
#endif
    memIO->pos += 2;
    return 0;
}

i32 MMemReadI32(MMemIO*restrict memIO, i32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    memmove(val, memIO->pos, 4);
    memIO->pos += 4;
    return 0;
}

i32 MMemReadI32LE(MMemIO*restrict memIO, i32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MBIGENDIAN
    i32 v;
    memmove(&v, memIO->pos, 4);
    *(val) = MLITTLEENDIAN32(v);
#else
    memmove(val, memIO->pos, 4);
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
    memmove(&v, memIO->pos, 4);
    *(val) = MBIGENDIAN32(v);
#else
    memmove(val, memIO->pos, 4);
#endif
    memIO->pos += 4;
    return 0;
}

i32 MMemReadU32(MMemIO*restrict memIO, u32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

    memmove(val, memIO->pos, 4);
    memIO->pos += 4;
    return 0;
}

i32 MMemReadU32LE(MMemIO*restrict memIO, u32*restrict val) {
    if (memIO->pos + 4 > memIO->mem + memIO->size) {
        return -1;
    }

#ifdef MBIGENDIAN
    i32 v;
    memmove(&v, memIO->pos, 4);
    *(val) = MLITTLEENDIAN32(v);
#else
    memmove(val, memIO->pos, 4);
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
    memmove(&v, memIO->pos, 4);
    *(val) = MBIGENDIAN32(v);
#else
    memmove(val, memIO->pos, 4);
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
