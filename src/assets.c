#include "assets.h"

i32 Assets_LoadModelPointers16LE(u8* data, u32 modelsToRead, ModelsArray* models) {
    MArrayInit(*models);
    MArrayGrow(*models, modelsToRead);
    u16* modelOffsets = (u16*)data;
    for (int i = 0; i < modelsToRead; ++i) {
        u16 offset = MLITTLEENDIAN16(modelOffsets[i]);
        if (offset == 0) {
            MArrayAdd(*models, NULL);
        } else {
            MArrayAdd(*models, (ModelData*)(((u8*)modelOffsets) + offset));
        }
    }
    return 1;
}

i32 Assets_LoadModelPointers16BE(u8* data, u32 modelsToRead, ModelsArray* models) {
    MArrayInit(*models);
    MArrayGrow(*models, modelsToRead);
    u16* modelOffsets = (u16*)data;
    for (int i = 0; i < modelsToRead; ++i) {
        u16 offset = MBIGENDIAN16(modelOffsets[i]);
        if (offset == 0) {
            MArrayAdd(*models, NULL);
        } else {
            MArrayAdd(*models, (ModelData*)(((u8*)modelOffsets) + offset));
        }
    }
    return 1;
}

i32 Assets_LoadModelPointers32BE(u8* data, u32 modelsToRead, ModelsArray* models) {
    MArrayInit(*models);
    MArrayGrow(*models, modelsToRead);
    u32* modelOffsets = (u32*)data;
    for (int i = 0; i < modelsToRead; ++i) {
        u32 offset = MBIGENDIAN32(modelOffsets[i]);
        if (offset == 0) {
            MArrayAdd(*models, NULL);
        } else {
            MArrayAdd(*models, (ModelData*)(((u8*)modelOffsets) + offset));
        }
    }
    return 1;
}

u8** Assets_LoadStringPointers16LE(u8* data, u32 stringsToRead) {
    u8** strings = (u8**)MMalloc(sizeof(u8*) * stringsToRead);
    u16* stringOffsets = (u16*)data;
    for (int i = 0; i < stringsToRead; ++i) {
        u16 offset = MLITTLEENDIAN16(stringOffsets[i]);
        strings[i] = (u8*)(((u8*)stringOffsets) + offset);
    }
    return strings;
}

u8** Assets_LoadStringPointers16BE(u8* data, u32 stringsToRead) {
    u8** strings = (u8**)MMalloc(sizeof(u8*) * stringsToRead);
    u16* stringOffsets = (u16*)data;
    for (int i = 0; i < stringsToRead; ++i) {
        u16 offset = MBIGENDIAN16(stringOffsets[i]);
        strings[i] = (u8*)(((u8*)stringOffsets) + offset);
    }
    return strings;
}

void Assets_EndianFlip16(u8* data, u32 sizeBytes) {
    u16* wordData = (u16*)data;
    u32 size = sizeBytes / 2;
    for (u32 i = 0; i < size; i++) {
        wordData[i] = MENDIANSWAP16(wordData[i]);
    }
}

void Assets_EndianFlip32(u8* data, u32 sizeBytes) {
    u32* dwordData = (u32*)data;
    u32 size = sizeBytes / 4;
    for (u32 i = 0; i < size; i++) {
        dwordData[i] = MENDIANSWAP32(dwordData[i]);
    }
}

void Assets_LoadAmigaFiles(AssetsDataAmiga* assets, MReadFileRet* amigaExeData, AssetsReadEnum assetsRead) {
    assets->mainExeData = amigaExeData->data;
    assets->mainExeSize = amigaExeData->size;

    if (assetsRead == AssetsRead_Amiga_Orig) {
        // Load model data
        Assets_LoadModelPointers16BE(amigaExeData->data + 0x387d6, 16, &assets->fontModels);
        ARRAY_REWRITE_BE16(amigaExeData->data + 0x387d6, 0x18fc);

        assets->bitmapFontData = amigaExeData->data + 0x3b034;

        assets->mainStrings = Assets_LoadStringPointers16BE(amigaExeData->data + 0x4c70d, 112);
    } else if (assetsRead == AssetsRead_Amiga_EliteClub || assetsRead == AssetsRead_Amiga_EliteClub2) {
        Assets_LoadModelPointers32BE(amigaExeData->data + 0x39ea0, 16, &assets->fontModels);
        ARRAY_REWRITE_BE16(amigaExeData->data + 0x3a328, 0x18fc);

        assets->bitmapFontData = amigaExeData->data + 0x3c9a6;

        assets->mainStrings = Assets_LoadStringPointers16BE(amigaExeData->data + 0x4ff9e, 126);
    }

    assets->assetsRead = assetsRead;
}

void Assets_FreeAmigaFiles(AssetsDataAmiga* assets) {
    MFree(assets->mainExeData); assets->mainExeData = 0;
    MArrayFree(assets->fontModels);
    MFree(assets->mainStrings);
}

MReadFileRet Assets_LoadModelOverrides(const char* filePath, ModelsArray* modelsArray) {
    MReadFileRet file = MFileReadFully(filePath);
    if (file.size) {
        u32 numModels = MBIGENDIAN32(*((u32*)file.data));
        Assets_LoadModelPointers32BE(file.data + 4, numModels, modelsArray);
    }
    return file;
}
