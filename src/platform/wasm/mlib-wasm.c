#include <stdlib.h>

#include "mlib.c"
#include "sprintf.c"
#include "wasm_funcs.h"

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
