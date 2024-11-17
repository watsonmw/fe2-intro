#ifndef FINTRO_ASSETS_H
#define FINTRO_ASSETS_H

#include "mlib.h"
#include "renderinternal.h"

#ifdef __cplusplus
extern "C" {
#endif

MARRAY_TYPEDEF(ModelData*, ModelsArray)

typedef enum eAssetsReadEnum {
    AssetsRead_Amiga_Orig,
    AssetsRead_Amiga_EliteClub,
    AssetsRead_Amiga_EliteClub2,
    AssetsRead_PC_EliteClub
} AssetsReadEnum;

typedef struct sAssetsData {
    u8* mainExeData;
    u32 mainExeSize;

    ModelsArray mainModels;
    ModelsArray introModels;
    ModelsArray galmapModels;
    u8** mainStrings;
    u32 mainStringsLen;
    u8* bitmapFontData;
    AssetsReadEnum assetsRead;
} AssetsData;

i32 Assets_LoadModelPointers16LE(u8* data, u32 modelsToRead, ModelsArray* modelsOut);
i32 Assets_LoadModelPointers16BE(u8* data, u32 modelsToRead, ModelsArray* modelsOut);
i32 Assets_LoadModelPointers32BE(u8* data, u32 modelsToRead, ModelsArray* modelsOut);

u8** Assets_LoadStringPointers16LE(u8* data, u32 stringsToRead);
u8** Assets_LoadStringPointers16BE(u8* data, u32 stringsToRead);

void Assets_EndianFlip16(u8* data, u32 sizeBytes);
void Assets_EndianFlip32(u8* data, u32 sizeBytes);

void Assets_LoadAmigaFiles(AssetsData* assets, MReadFileRet* amigaExeData, AssetsReadEnum assetsReadEnum);
void Assets_LoadAmigaMainModels(AssetsData* assets);
void Assets_LoadAmigaIntroModels(AssetsData* assets);
void Assets_Free(AssetsData* assets);

MReadFileRet Assets_LoadModelOverrides(const char* filePath, ModelsArray* modelsArray);

#if MLITTLEENDIAN
#define ARRAY_REWRITE_BE16(data, sizeBytes) Assets_EndianFlip16(data, sizeBytes)
#define ARRAY_REWRITE_BE32(data, sizeBytes) Assets_EndianFlip32(data, sizeBytes)
#define ARRAY_REWRITE_LE16(data, sizeBytes) while(FALSE) {}
#define ARRAY_REWRITE_LE32(data, sizeBytes) while(FALSE) {}
#else
#define ARRAY_REWRITE_BE16(data, sizeBytes) while(FALSE) {}
#define ARRAY_REWRITE_BE32(data, sizeBytes) while(FALSE) {}
#define ARRAY_REWRITE_LE16(data, sizeBytes) Assets_EndianFlip16(data, sizeBytes)
#define ARRAY_REWRITE_LE32(data, sizeBytes) Assets_EndianFlip32(data, sizeBytes)
#endif

#ifdef __cplusplus
}
#endif

#endif