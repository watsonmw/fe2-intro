#pragma once

#include "mlib.h"

// Public Domain malloc() / realloc() / free() implementation
// Simple free list allocator
// - Lots of debug checking
// - Free list is always in memory order
// - Node/Block size is large, so don't allocate very many small objects
// - Not thread safe
// - Can request more address space (via memoryGrowFunc()) but memory must be contiguous

// Min alignment for allocations
#define MBASIC_ALLOC_ALIGN 16

typedef struct MBasicAllocNode {
    size_t size;
    u32 free;
    struct MBasicAllocNode* prevNode;
    struct MBasicAllocNode* nextFree;
} MBasicAllocNode;

typedef size_t (*M_MemGrowMemory_t)(u8* newEnd, size_t oldSize, size_t newSize);

typedef struct MBasicAlloc {
    MAllocator funcs;
    u8* mem;     // start of available memory
    u8* end;     // points just past the final node of memory used (somewhere between mem and mem+size)
    size_t size; // full size of available memory, only used up to 'end'
    MBasicAllocNode* freeList;
    MBasicAllocNode* lastNode;
    M_MemGrowMemory_t memoryGrowFunc;
} MBasicAlloc;

typedef struct MBasicStats {
    size_t memoryAvailable;
    size_t memoryUsed;
    size_t allocatedSpace;
    size_t allocatedSlots;
    size_t freeSpace;
    size_t freeSlots;
} MBasicStats;

MBasicAlloc* MBasicAlloc_Init(void* mem, size_t size);
void* MBasicAlloc_Malloc(MBasicAlloc* a, size_t size);
void* MBasicAlloc_Realloc(MBasicAlloc* a, void* ptr, size_t oldSize, size_t size);
void MBasicAlloc_Free(MBasicAlloc* a, void* ptr, size_t size);

// Debug & info functions
MBasicAllocNode* MBasicAlloc_GetMallocNode(MBasicAlloc* a, void* ptr);
MINLINE MBasicAllocNode* MBasicAlloc_GetFirstMallocNode(MBasicAlloc* a)
{ return (MBasicAllocNode*)(a->mem + MSizeAlign(sizeof(MBasicAlloc), MBASIC_ALLOC_ALIGN)); }
MINLINE MBasicAllocNode* MBasicAlloc_GetNextMallocNode(MBasicAlloc* a, MBasicAllocNode* node)
{ return (MBasicAllocNode*)((u8*)node + node->size); }

void MBasicAlloc_CheckAll(MBasicAlloc* a);
void MBasicAlloc_GetStats(MBasicAlloc* a, MBasicStats* basicStats);
void MBasicAlloc_PrintStats(MBasicAlloc* a);
void MBasicAlloc_PrintNodes(MBasicAlloc* a);
