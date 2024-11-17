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

void Assets_LoadAmigaFiles(AssetsData* assets, MReadFileRet* amigaExeData, AssetsReadEnum assetsRead) {
    assets->mainExeData = amigaExeData->data;
    assets->mainExeSize = amigaExeData->size;

    if (assetsRead == AssetsRead_Amiga_Orig) {
        // Load model data
        Assets_LoadModelPointers16BE(amigaExeData->data + 0x387d6, 16, &assets->galmapModels);
        ARRAY_REWRITE_BE16(amigaExeData->data + 0x387d6, 0x18fc);

        assets->bitmapFontData = amigaExeData->data + 0x3b034;

        assets->mainStrings = Assets_LoadStringPointers16BE(amigaExeData->data + 0x4c70d, 112);
        assets->mainStringsLen = 112;
    } else if (assetsRead == AssetsRead_Amiga_EliteClub || assetsRead == AssetsRead_Amiga_EliteClub2) {
        Assets_LoadModelPointers32BE(amigaExeData->data + 0x39ea0, 16, &assets->galmapModels);
        ARRAY_REWRITE_BE16(amigaExeData->data + 0x3a328, 0x18fc);

        assets->bitmapFontData = amigaExeData->data + 0x3c9a6;

        assets->mainStrings = Assets_LoadStringPointers16BE(amigaExeData->data + 0x4ff9e, 126);
        assets->mainStringsLen = 126;
    }

    assets->assetsRead = assetsRead;
}

void Assets_LoadAmigaMainModels(AssetsData* assets) {
    u8* fileData = assets->mainExeData;
    if (assets->assetsRead == AssetsRead_Amiga_Orig) {
        // Load model data
        Assets_LoadModelPointers16BE(fileData + 0x28804, 300, &assets->mainModels);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x289e4, 0xffee);
    } else if (assets->assetsRead == AssetsRead_Amiga_EliteClub) {
        Assets_LoadModelPointers32BE(fileData + 0x28804, 300, &assets->mainModels);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x28c8c, 0x11698);
    } else if (assets->assetsRead == AssetsRead_Amiga_EliteClub2) {
        Assets_LoadModelPointers32BE(fileData + 0x28804, 300, &assets->mainModels);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x28c8c, 0x11698);

        ARRAY_REWRITE_BE16(fileData + 0x310fe, 92); // 61
        ARRAY_REWRITE_BE16(fileData + 0x31472, 398); // 63
        ARRAY_REWRITE_BE16(fileData + 0x31960, 142); // 65
        ARRAY_REWRITE_BE16(fileData + 0x31b4c, 92); // 69
        ARRAY_REWRITE_BE16(fileData + 0x31bf6, 76); // 70
        ARRAY_REWRITE_BE16(fileData + 0x31d58, 336); // 72
        ARRAY_REWRITE_BE16(fileData + 0x32236, 86); // 73
        ARRAY_REWRITE_BE16(fileData + 0x3234a, 72); // 74
        ARRAY_REWRITE_BE16(fileData + 0x32452, 16); // 75
        ARRAY_REWRITE_BE16(fileData + 0x324cc, 72); // 76
    }
}

void Assets_LoadAmigaIntroModels(AssetsData* assets) {
    u8* fileData = assets->mainExeData;
    if (assets->assetsRead == AssetsRead_Amiga_Orig) {
        // Load model data
        u8* modelDataStart = fileData + 0x77160;
        Assets_LoadModelPointers16BE(modelDataStart, 120, &assets->introModels);
        ARRAY_REWRITE_BE16(fileData + 0x387d6, 0x18fc);

        u32 planetByteCodeLen = 28;
        u32 planetByteCodeStart = 0x2772;
        ARRAY_REWRITE_BE16(modelDataStart, planetByteCodeStart);
        ARRAY_REWRITE_BE16((modelDataStart + planetByteCodeStart + planetByteCodeLen),
                           (0x6a34 - (planetByteCodeStart + planetByteCodeLen)));
    } else if (assets->assetsRead == AssetsRead_Amiga_EliteClub) {
        Assets_LoadModelPointers32BE(fileData + 0x7ffdc, 120, &assets->introModels);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x80464, 0x687c);

        // Flip back planet byte code
        ARRAY_REWRITE_BE16(fileData + 0x829f8, 28);
    } else if (assets->assetsRead == AssetsRead_Amiga_EliteClub2) {
        Assets_LoadModelPointers32BE(fileData + 0x7ffdc, 120, &assets->introModels);

        // Flip model words
        ARRAY_REWRITE_BE16(fileData + 0x80464, 0x687c);

        // Flip back planet byte code
        ARRAY_REWRITE_BE16(fileData + 0x829f8, 28);
    }
}

void Assets_Free(AssetsData* assets) {
    MFree(assets->mainExeData, assets->mainExeSize); assets->mainExeData = 0;
    MArrayFree(assets->mainModels);
    MArrayFree(assets->introModels);
    MArrayFree(assets->galmapModels);
    MFree(assets->mainStrings, assets->mainStringsLen * sizeof(u8*));
}

MReadFileRet Assets_LoadModelOverrides(const char* filePath, ModelsArray* modelsArray) {
    MReadFileRet file = MFileReadFully(filePath);
    if (file.size) {
        u32 numModels = MBIGENDIAN32(*((u32*)file.data));
        Assets_LoadModelPointers32BE(file.data + 4, numModels, modelsArray);
    }
    return file;
}
