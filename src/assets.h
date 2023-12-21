#ifndef FINTRO_ASSETS_H
#define FINTRO_ASSETS_H

#include "mlib.h"
#include "renderinternal.h"

#ifdef __cplusplus
extern "C" {
#endif

MARRAY_TYPEDEF(ModelData*, ModelsArray)

typedef struct sAssetsData {
    u8* mainData;
    u8* bitmapFontData;

    u8** mainStrings;

    ModelsArray models;
    ModelsArray galmapModels;
} AssetsData;

typedef struct sAssetsDataPC {
    u8* mainExeData;
    u32 mainExeSize;
    u8* videoModuleData;
    u32 videoModuleSize;
    u8* gameMenuModuleData;
    u32 gameMenuModuleSize;
    u8* introModuleData;
    u32 introModuleSize;

    u8* mainStringData;
    u8* vectorFontData;
    u8* bitmapFontData;
    u8* defaultPalette;
} AssetsDataPC;

typedef enum eAssetsReadEnum {
    AssetsRead_Amiga_Orig,
    AssetsRead_Amiga_EliteClub,
    AssetsRead_Amiga_EliteClub2,
    AssetsRead_PC_EliteClub
} AssetsReadEnum;

typedef struct sAssetsDataAmiga {
    u8* mainExeData;
    u32 mainExeSize;

    ModelsArray galmapModels;
    u8** mainStrings;
    u32 mainStringsLen;
    u8* bitmapFontData;
    AssetsReadEnum assetsRead;
} AssetsDataAmiga;

i32 Assets_LoadModelPointers16LE(u8* data, u32 modelsToRead, ModelsArray* modelsOut);
i32 Assets_LoadModelPointers16BE(u8* data, u32 modelsToRead, ModelsArray* modelsOut);
i32 Assets_LoadModelPointers32BE(u8* data, u32 modelsToRead, ModelsArray* modelsOut);

u8** Assets_LoadStringPointers16LE(u8* data, u32 stringsToRead);
u8** Assets_LoadStringPointers16BE(u8* data, u32 stringsToRead);

void Assets_EndianFlip16(u8* data, u32 sizeBytes);
void Assets_EndianFlip32(u8* data, u32 sizeBytes);

MReadFileRet Assets_LoadAmigaExeFromDataDir(AssetsReadEnum assetsRead);
void Assets_LoadAmigaFiles(AssetsDataAmiga* assets, MReadFileRet* amigaExeData, AssetsReadEnum assetsReadEnum);
void Assets_FreeAmigaFiles(AssetsDataAmiga* assets);

MReadFileRet Assets_LoadModelOverrides(const char* filePath, ModelsArray* modelsArray);

#ifdef __GNUC__
#if defined(__i386__) || defined(__x86_64) || defined(__EMSCRIPTEN__) || defined(WASM_DIRECT)
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
#elif defined(_MSC_VER)
#define ARRAY_REWRITE_BE16(data, sizeBytes) Assets_EndianFlip16(data, sizeBytes)
#define ARRAY_REWRITE_BE32(data, sizeBytes) Assets_EndianFlip32(data, sizeBytes)
#define ARRAY_REWRITE_LE16(data, sizeBytes) while(FALSE) {}
#define ARRAY_REWRITE_LE32(data, sizeBytes) while(FALSE) {}
#elif defined(__VBCC__)
#define ARRAY_REWRITE_BE16(data, sizeBytes) while(FALSE) {}
#define ARRAY_REWRITE_BE32(data, sizeBytes) while(FALSE) {}
#define ARRAY_REWRITE_LE16(data, sizeBytes) Assets_EndianFlip16(data, sizeBytes)
#define ARRAY_REWRITE_LE32(data, sizeBytes) Assets_EndianFlip32(data, sizeBytes)
#endif

#ifdef __cplusplus
}
#endif

#endif