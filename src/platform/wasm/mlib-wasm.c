#include <stdlib.h>
#include <string.h>

#include "mlib.c"
#include "sprintf.c"
#include "wasm_funcs.h"

////////////////////////////////////////
// Malloc implementation

// These symbols are located at start and end of heap (i.e. don't access them they are just markers!)
extern i32 __heap_base;

#define MALLOC_ALIGN 8
#define MALLOC_PAGE_SIZE 0x1000
#define WASM_BLOCK_SIZE 0x10000

typedef struct sMallocNode {
    struct sMallocNode* next;
    size_t size;
    b32 free;
    void* ptr;
} MallocNode;

static u8* sMallocMemBase;
static u8* sMallocMemEnd;
static u8* sMallocMemFree;
static MallocNode* sMallocListHead;

// Add new space to end of heap
void* Malloc_GetHeapPtrForSize(size_t size) {
    if (sMallocMemBase == 0) {
        sMallocMemBase = (u8*)&__heap_base;
        sMallocMemFree = sMallocMemBase;
        sMallocMemEnd = (u8*)(__builtin_wasm_memory_size(0) * WASM_BLOCK_SIZE);
    }
    u8* mem = sMallocMemFree;
    u8* newEnd = mem + size;
    if (newEnd > sMallocMemEnd) {
        // TODO: attempt to grow heap space here
        MLog("Out of memory!!!  Increase initial WASM memory.");
        return NULL;
    }
    MLogf("Added more mem %p [%d]", mem, size);
    sMallocMemFree = newEnd;
    return mem;
}

// Get nearest multiple of page size that fits the requested size
MINLINE size_t Malloc_GetPageSizeForSize(size_t size, size_t pageSize) {
    size_t result = pageSize;

    size_t i = 1;
    while (result < size) {
        result = pageSize * ++i;
    }

    return result;
}

MINLINE void* Malloc_AlignPointer(void* ptr) {
    return (void*)((((size_t)ptr) + MALLOC_ALIGN - 1) & ~(size_t)(MALLOC_ALIGN - 1));
}

// Split node in two:
//  - Given node is resized to the given size
//  - A new node is inserted after to fill the gap
// If there's not enough room to split the node, just return.
void Malloc_SplitNode(MallocNode* cnt, size_t size) {
    size_t newSize = size + sizeof(MallocNode);
    void* nextPtr = Malloc_AlignPointer(cnt->ptr + newSize);
    size_t wouldBeSize = nextPtr - cnt->ptr;
    if (wouldBeSize >= cnt->size) {
        MLogf("Not enough space for %p [%d]", cnt->ptr, size);
        return;
    }
    MallocNode *new = (MallocNode*)(cnt->ptr + size);
    new->next = cnt->next;
    new->size = wouldBeSize - size;
    new->free = 1;
    new->ptr = cnt->ptr + newSize;
    cnt->next = new;
    cnt->size = size;
}

void free(void *ptr) {
    MallocNode* cnt = sMallocListHead;
    while (cnt != NULL) {
        if (cnt->ptr == ptr) {
            cnt->free = 1;
            MLogf("free(): %p [%d]", ptr, cnt->size);
            return;
        }

        if ((u8*) cnt->next > sMallocMemEnd) {
            MLog("oops");
        }
        cnt = cnt->next;
    }
    MLogf("Invalid free(): %p", ptr);
}

void* malloc(size_t size) {
    MallocNode* cnt = sMallocListHead;
    MallocNode* prev = NULL;
    size_t pageSize = MALLOC_PAGE_SIZE;
    size_t totalSize = size + sizeof(MallocNode);

    while (cnt != NULL) {
        if (cnt->free) {
            if (cnt->size >= size) {
                if (cnt->size > size) {
                    Malloc_SplitNode(cnt, size);
                }

                MLogf("malloc() split %p", cnt->ptr);

                cnt->free = 0;
                return cnt->ptr;
            }
        }

        prev = cnt;
        cnt = cnt->next;
    }

    size_t newSize = Malloc_GetPageSizeForSize(totalSize, pageSize);
    void* ptr = Malloc_GetHeapPtrForSize(newSize);
    if (ptr == NULL) {
        return NULL;
    }

    cnt = (MallocNode*)ptr;
    cnt->next = NULL;
    cnt->free = 0;
    cnt->size = newSize - sizeof(MallocNode);
    cnt->ptr = Malloc_AlignPointer(ptr + sizeof(MallocNode));

    if (pageSize > totalSize) {
        Malloc_SplitNode(cnt, size);
    }

    if (sMallocListHead == NULL) {
        sMallocListHead = cnt;
    } else if (prev != NULL) {
        prev->next = cnt;
    }

    MLogf("malloc() added new %p", cnt->ptr);

    return cnt->ptr;
}

void* realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return malloc(size);
    }

    if (size == 0) {
        free(ptr);
        return ptr;
    }

    MallocNode* cnt = sMallocListHead;
    while (cnt != NULL) {
        if (cnt->ptr == ptr) {
            break;
        }
        cnt = cnt->next;
    }

    if (cnt != NULL) {
        if ((cnt->size > size) && (cnt->size - size > MALLOC_ALIGN)) {
            Malloc_SplitNode(cnt, size);
            return ptr;
        } else if (cnt->size < size) {
            void* new = malloc(size);
            memcpy(new, cnt->ptr, cnt->size);
            return new;
        } else {
            return ptr;
        }
    }

    return NULL;
}


MReadFileRet MFileReadWithOffset(const char* filePath, u32 offset, u32 readSize) {
    MReadFileRet ret;
    ret.size = 0;
    ret.data = NULL;

    u32 filePathSize = MStrLen(filePath);

    if (!readSize) {
        readSize = WASM_FileGetSize(filePath, filePathSize);
    }

    u8* data = (u8*)MMalloc(readSize);
    ret.data = data;

    size_t bytesRead = WASM_FileRead(filePath, filePathSize, ret.data, offset, readSize);
    ret.size = bytesRead;

    return ret;
}

#define VSNPRINTF_MINUS_1_RETRY 1

i32 MStringAppendfv(MMemIO* memIo, const char* format, va_list vargs1) {
    u32 writableLen = memIo->capacity - memIo->size;

    va_list vargs2;
    va_copy(vargs2, vargs1);

    i32 size;

    if (writableLen <= 1) {
        // No space to write any string - calc size of format string and add the space
        size = vsnprintf(NULL, 0, format, vargs1);
        if (size > 0) {
            u32 sizeAdded = size + 1;
            MMemGrowBytes(memIo, sizeAdded);
            size = vsnprintf((char*)memIo->pos, sizeAdded, format, vargs2);
        }
    } else {
        // Space to write some string, but it might not be enough
        size = vsnprintf((char*)memIo->pos, writableLen, format, vargs1);
        if ((size + 1) > writableLen) {
            // Not enough space to fit format string
            u32 sizeAdded = size + 1;
            MMemGrowBytes(memIo, sizeAdded);
            size = vsnprintf((char*)memIo->pos, sizeAdded, format, vargs2);
        }
#if VSNPRINTF_MINUS_1_RETRY == 1
        else if (size < 0) {
            // Size of the buffer too small, call again to find required size
            size = vsnprintf(NULL, 0, format, vargs1);
            if (size > 0) {
                u32 sizeAdded = size + 1;
                MMemGrowBytes(memIo, sizeAdded);
                size = vsnprintf((char*)memIo->pos, sizeAdded, format, vargs2);
            }
        }
#endif
        // else successfully wrote without adding new space
    }

    if (size < 0) {
        MLogf("Encoding error for sprintf format: %s", format);
    } else {
        memIo->pos += size;
        memIo->size += size;
    }

    va_end(vargs2);

    return size;
}

i32 MStringAppendf(MMemIO* memIo, const char* format, ...) {
    va_list vargs;
    va_start(vargs, format);
    i32 size = MStringAppendfv(memIo, format, vargs);
    va_end(vargs);
    return size;
}

////////////////////////////////////////
// MLogging

// This buffer must be long enough to hold any message printed during malloc() / realloc() / free(). This is because we
// can't resize the buffer while in the middle of printing a log message.  Right now these log messages are short, but
// if they get longer either this buffer has to be increased or these function made reentrant.
static u8 sMemLoggingBuffer[1024];
static MMemIO sMemIo = {sMemLoggingBuffer, sMemLoggingBuffer, 0, sizeof(sMemLoggingBuffer)};

void MLogf(const char *format, ...) {
    va_list vargs;
    va_start(vargs, format);
    MMemReset(&sMemIo);
    MStringAppendfv(&sMemIo, format, vargs);
    va_end(vargs);
    WASM_PrintLine((char*)sMemIo.mem,sMemIo.size);
}

void MLogfNoNewLine(const char *format, ...) {
    va_list vargs;
    va_start(vargs, format);
    MMemReset(&sMemIo);
    MStringAppendfv(&sMemIo, format, vargs);
    va_end(vargs);
    WASM_Print((char*)sMemIo.mem,sMemIo.size);
}

void MLog(const char *str) {
    WASM_PrintLine(str, MStrLen(str));
}
