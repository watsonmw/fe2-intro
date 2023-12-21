#include <stdio.h>

#include <SDL2/SDL.h>

#include "mlib.c"
#include "platform/mlib-log-stdlib.c"

MReadFileRet MFileReadWithOffset(const char* filePath, u32 offset, u32 readSize) {
    MReadFileRet ret;
    ret.size = 0;
    ret.data = NULL;

    SDL_RWops *file = SDL_RWFromFile(filePath, "rb");
    if (file == NULL) {
        return ret;
    }

    u32 size = readSize;
    if (!readSize) {
        size = SDL_RWsize(file);
    }

    ret.data = (u8*)MMalloc(size);

    if (offset) {
        SDL_RWseek(file, offset, RW_SEEK_SET);
    }

    size_t bytesRead = SDL_RWread(file, ret.data, 1, size);
    ret.size = bytesRead;

    SDL_RWclose(file);

    return ret;
}

i32 MFileWriteDataFully(const char* filePath, u8* data, u32 size) {
    SDL_RWops *file = SDL_RWFromFile(filePath, "wb");
    if (file == NULL) {
        return 0;
    }

    size_t sizeWritten = SDL_RWwrite(file, data, size, 1);
    SDL_RWclose(file);
    return (i32)sizeWritten;
}

MFile MFileWriteOpen(const char* filePath) {
    MFile fileData;

    SDL_RWops *file = SDL_RWFromFile(filePath, "w");
    if (file == NULL) {
        fileData.open = 0;
        return fileData;
    }

    fileData.handle = file;
    fileData.open = 1;

    return fileData;
}

i32 MFileWriteData(MFile* file, u8* data, u32 size) {
    if (!file->open) {
        return -1;
    }

    return (i32)SDL_RWwrite(file->handle, data, size, 1);
}

void MFileClose(MFile* file) {
    if (file->open) {
        SDL_RWclose((SDL_RWops*)file->handle);
        file->open = 0;
        file->handle = 0;
    }
}

