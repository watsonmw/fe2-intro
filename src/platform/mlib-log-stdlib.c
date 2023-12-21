#include <stdarg.h>
#include <stdio.h>

#include "mlib.h"

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

// -1 is returned in some cases when the size of the buffer is too small.
// Visual Studio only does this when the encoding is incorrect, but GCC will do it when the buffer is too small to
// contain the output.
#define VSNPRINTF_MINUS_1_RETRY 1

i32 MStringAppendf(MMemIO* memIo, const char* format, ...) {
    u32 writableLen = memIo->capacity - memIo->size;

    va_list vargs1;

    va_start(vargs1, format);
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

    va_end(vargs1);
    va_end(vargs2);

    return size;
}
