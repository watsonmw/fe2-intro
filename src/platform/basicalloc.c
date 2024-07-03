#include "mlib.h"

#include "basicalloc.h"

// Simple free list allocator
// - Lots of debug checking
// - Free list is always in memory order
// - Node size is large, so don't allocate very many small objects
// - Not thread safe
// - Can request more address space (via memoryGrowFunc()) but memory must be contiguous

// Allocate memory in blocks of this size (to prevent fragmentation)
// You can set to MBASIC_ALLOC_ALIGN if you want the absolute minimum size at the expense of possible
// extra fragmentation.
#define MBASIC_ALLOC_PAGE_SIZE 0x1000

MINLINE u8* M_MemPtrFromNode(MBasicAllocNode* node) {
    return (u8*)node + MSizeAlign(sizeof(MBasicAllocNode), MBASIC_ALLOC_ALIGN);
}

MBasicAllocNode* MBasicAlloc_GetMallocNode(MBasicAlloc* a, void *ptr) {
    size_t offset = MSizeAlign(sizeof(MBasicAllocNode), MBASIC_ALLOC_ALIGN);
    MBasicAllocNode* node = ptr - offset;
    return node;
}

MINLINE b32 M_CheckNode(MBasicAlloc* a, MBasicAllocNode* node) {
    u8* nodeMem = (u8*)node;
    if (nodeMem + sizeof(MBasicAllocNode) >= a->end || nodeMem < (u8*)a) {
        MBreakpointf("Mode header outside memory range %p", node);
        return FALSE;
    }
    if ((u8*)node + node->size > a->end) {
        MBreakpointf("Mode memory outside memory range %p %p", node, a->end);
        return FALSE;
    }
    return TRUE;
}

MINLINE void M_NodeAddToFreeList(MBasicAlloc* a, MBasicAllocNode* node) {
    node->free = 1;

    MBasicAllocNode* next_node = MBasicAlloc_GetNextMallocNode(a, node);
    MBasicAllocNode* next_free = NULL;

    // Merge with next node if it is also free
    if (next_node != NULL && (u8*)next_node < a->end) {
        if (next_node->free) {
            node->size = (((u8 *) next_node + next_node->size) - (u8 *) node);
            next_free = next_node->nextFree;
            if (a->lastNode == next_node) {
                a->lastNode = node;
            }
        } else {
            next_free = next_node;
            do {
                next_free = MBasicAlloc_GetNextMallocNode(a, next_free);
                if ((u8*)next_free >= a->end) {
                    next_free = NULL;
                    break;
                }
            } while (!next_free->free);
        }
    }

    // Merge with prev node if it is also free
    MBasicAllocNode* prev_node = node->prevNode;
    if (prev_node != NULL) {
        // check:
        if (prev_node->free) {
            prev_node->size = (((u8*)node + node->size) - (u8*)prev_node);
            if (a->lastNode == node) {
                a->lastNode = prev_node;
            }
            node = prev_node;
        }
    }

    node->nextFree = next_free;

    // Update the nearest free node before this node to point to here.
    prev_node = node->prevNode;
    while (prev_node != NULL) {
        if (prev_node->free) {
            prev_node->nextFree = node;
            break;
        }
        prev_node = prev_node->prevNode;
    }
    if (prev_node == NULL) {
        a->freeList = node;
    }

    // Update next node to point this one
    next_node = MBasicAlloc_GetNextMallocNode(a, node);
    if (next_node != NULL && (u8*)next_node < a->end) {
        next_node->prevNode = node;
    }
}

MINLINE void M_NodeSplit(MBasicAlloc *a, MBasicAllocNode *node, size_t newSize, MBasicAllocNode* nextFree) {
    MBasicAllocNode* newNode = (MBasicAllocNode*)((u8*)node + newSize);
    newNode->size = 0;
    newNode->size = (node->size - newSize);
    newNode->nextFree = nextFree;
    newNode->prevNode = node;
    node->size = newSize;
    if (a->lastNode == node) {
        a->lastNode = newNode;
    }
    M_NodeAddToFreeList(a, newNode);
}

void MBasicAlloc_Free(MBasicAlloc* a, void *ptr, size_t size) {
#ifdef M_MEM_DEBUG
    if (ptr == NULL) {
        MBreakpoint("free() null mem");
        return;
    }
#endif

    MBasicAllocNode* node = MBasicAlloc_GetMallocNode(a, ptr);
#ifdef M_MEM_DEBUG
    if (node == NULL) {
        MBreakpointf("free %p - not found", ptr);
        return;
    }

    if (node->free) {
        MBreakpointf("free on pointer that was previously freed: %p", ptr);
        return;
    }
#endif

    M_NodeAddToFreeList(a, node);

#ifdef M_MEM_DEBUG
    MBasicAlloc_CheckAll(a);
#endif
}

void* MBasicAlloc_Malloc(MBasicAlloc* a, size_t size) {
    MBasicAllocNode* cnt = a->freeList;
    MBasicAllocNode* prev = NULL;
    size_t mallocSizePlusHeader = MSizeAlign(sizeof(MBasicAllocNode), MBASIC_ALLOC_ALIGN) + size;
    size_t nearestPageSize = MSizeAlign(mallocSizePlusHeader, MBASIC_ALLOC_PAGE_SIZE);

    while (cnt != NULL) {
        MAssert(cnt->free, "Non free node in free list");
        if (cnt->size >= nearestPageSize) {
            if (prev) {
                prev->nextFree = cnt->nextFree;
            } else {
                a->freeList = cnt->nextFree;
            }
            cnt->free = 0;
            if (cnt->size > nearestPageSize) {
                M_NodeSplit(a, cnt, nearestPageSize, cnt->nextFree);
            }
            goto done;
        }
        prev = cnt;
        cnt = cnt->nextFree;
    }

    u8* newEnd = a->end + nearestPageSize;
    if (newEnd - a->mem > a->size) {
        if (a->memoryGrowFunc) {
            a->size += a->memoryGrowFunc(a->mem, a->size, newEnd - a->mem);
        } else {
            MBreakpointf("Out of memory while trying to allocate %d bytes", size);
        }
    }

    MBasicAllocNode* newNode = (MBasicAllocNode*)a->end;
    newNode->free = 0;
    newNode->nextFree = 0;
    newNode->size = nearestPageSize;
    newNode->prevNode = a->lastNode;
    a->end = newEnd;
    a->lastNode = newNode;
    cnt = newNode;

    done:
#ifdef M_MEM_DEBUG
    MBasicAlloc_CheckAll(a);
#endif
    return M_MemPtrFromNode(cnt);
}

void* MBasicAlloc_Realloc(MBasicAlloc* a, void *ptr, size_t oldSize, size_t size) {
    if (ptr == NULL) {
        return MBasicAlloc_Malloc(a, size);
    }

    if (size == 0) {
        MBasicAlloc_Free(a, ptr, oldSize);
        return ptr;
    }

    MBasicAllocNode* node = MBasicAlloc_GetMallocNode(a, ptr);
#ifdef M_MEM_DEBUG
    if (node == NULL) {
        MBreakpointf("realloc %p - allocation not found", ptr);
        return NULL;
    }

    if (node->free) {
        MBreakpointf("realloc on pointer that was previously freed: %p", ptr);
        return NULL;
    }
#endif
    if (node->size >= size + MSizeAlign(sizeof(MBasicAllocNode), MBASIC_ALLOC_ALIGN)) {
        return M_MemPtrFromNode(node);
    } else {
        void* newMem = MBasicAlloc_Malloc(a, size);
        memmove(newMem, ptr, oldSize);
        MBasicAlloc_Free(a, ptr, oldSize);
        return newMem;
    }
}

MBasicAlloc* MBasicAlloc_Init(void *mem, size_t size) {
    MBasicAlloc *a = mem;
    a->mem = mem;
    a->end = MPtrAlign(mem + sizeof(MBasicAlloc), MBASIC_ALLOC_ALIGN);
    a->size = size;
    a->lastNode = NULL;
    a->freeList = NULL;
    a->funcs.mallocFunc = (M_malloc_t)MBasicAlloc_Malloc;
    a->funcs.reallocFunc = (M_realloc_t)MBasicAlloc_Realloc;
    a->funcs.freeFunc = (M_free_t)MBasicAlloc_Free;
    a->memoryGrowFunc = NULL;
    return a;
}

void MBasicAlloc_GetStats(MBasicAlloc* a, MBasicStats* basicStats) {
    basicStats->allocatedSlots = 0;
    basicStats->allocatedSpace = 0;
    basicStats->freeSlots = 0;
    basicStats->freeSpace = 0;

    MBasicAllocNode* node = MBasicAlloc_GetFirstMallocNode(a);
    while (node != NULL && (u8*)node < a->end) {
        if (node->free) {
            basicStats->freeSlots += 1;
            basicStats->freeSpace += node->size;
        } else {
            basicStats->allocatedSlots += 1;
            basicStats->allocatedSpace += node->size;
        }

        node = (MBasicAllocNode*)((u8*)node + node->size);
    }

    basicStats->memoryAvailable = a->size;
    basicStats->memoryUsed = a->end - a->mem;
}

void MBasicAlloc_PrintStats(MBasicAlloc* a) {
    MBasicStats aStats;
    MBasicAlloc_GetStats(a, &aStats);
    MLogf("  Reserved: %zu bytes Used: %zu bytes", aStats.memoryAvailable, aStats.memoryUsed);
    MLogf("  Allocated: %zu slots %zu bytes", aStats.allocatedSlots, aStats.allocatedSpace);
    MLogf("  Free: %zu slots %zu bytes", aStats.freeSlots, aStats.freeSpace);
}

void MBasicAlloc_PrintNodes(MBasicAlloc* a) {
    MBasicAllocNode* node = MBasicAlloc_GetFirstMallocNode(a);
    MLogf("Heap nodes (ptr, size, free):");
    while (node != NULL && (u8*)node < a->end) {
        MLogf("    %p, %x (%d), %d", node, node->size, node->size, node->free);
        node = (MBasicAllocNode*)((u8*)node + node->size);
    }
}

void MBasicAlloc_CheckAll(MBasicAlloc* a) {
    MBasicAllocNode* node = MBasicAlloc_GetFirstMallocNode(a);
    MBasicAllocNode* lastNode = NULL;
    int foundFreeNodes = 0;
    while (node != NULL && (u8*)node < a->end) {
        if (node->prevNode != lastNode) {
            MBreakpoint("Prev node not correct!");
        }
        M_CheckNode(a, node);
        if (node->free) {
            foundFreeNodes += 1;
        }
        lastNode = node;
        node = (MBasicAllocNode*)((u8*)node + node->size);
    }
    if (lastNode != a->lastNode) {
        MBreakpoint("Last node not correct!");
    }

    node = a->freeList;
    int freeListNodes = 0;
    while (node != NULL) {
        M_CheckNode(a, node);
        node = node->nextFree;
        freeListNodes++;
        if (freeListNodes > foundFreeNodes) {
            MBreakpointf("Too many steps while traversing free list - probably has a loop - > %d nodes",
                         foundFreeNodes);
            return;
        }
    }
    if (freeListNodes < foundFreeNodes) {
        MBreakpointf("%d free nodes on free list, but %d free nodes in memory", freeListNodes, foundFreeNodes);
    }
}
