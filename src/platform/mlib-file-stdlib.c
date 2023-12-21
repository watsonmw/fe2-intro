#include "mlib.h"

#include <stdio.h>

MReadFileRet MFileReadWithOffset(const char* filePath, u32 offset, u32 readSize) {
    MReadFileRet ret;
    ret.size = 0;
    ret.data = NULL;

    FILE *file = fopen(filePath, "rb");
    if (file == NULL) {
        return ret;
    }

    u32 size = readSize;
    if (!readSize) {
        fseek(file, 0L, SEEK_END);
        size = ftell(file);
        fseek(file, 0L, SEEK_SET);
    }

    ret.data = (u8*)MMalloc(size);

    if (offset) {
        fseek(file, offset, SEEK_SET);
    }

    size_t bytesRead = fread(ret.data, 1, size, file);
    ret.size = bytesRead;

    fclose(file);

    return ret;
}

MFile MFileWriteOpen(const char* filePath) {
    MFile fileData;

    FILE *file = fopen(filePath, "wb");
    if (file == NULL) {
        fileData.open = 0;
        return fileData;
    } else {
        fileData.handle = file;
        fileData.open = 1;
    }

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
