#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

#include "mcpplib.h"

#ifdef __GNUC__
#include <sys/stat.h>
#endif

int MStrWriter::appendf(const char* format, ...) {
    char buff[256];
    va_list vargs;
    va_start(vargs, format);
    int size = vsnprintf(buff, 256, format, vargs);
    va_end(vargs);
    append(buff);
    return size;
}

MFileInfo MGetFileInfo(const char* filePath) {
    MFileInfo fileInfo;
    memset(&fileInfo, 0, sizeof(fileInfo));
#if defined(__APPLE__)
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
    return fileInfo;
}
