#pragma once

#include "mlib.h"

#ifdef __cplusplus
extern "C" {
#endif

/////////////////////////////////////////////////////////
// Simple ini file reading / writing
// ini scopes not supported

typedef struct {
    char *key;
    u32 keySize;

    char *val;
    u32 valSize;
} MIniPair;

MARRAY_TYPEDEF(MIniPair, MIniPairs)

typedef struct {
    u8 *data;
    u32 dataSize;
    MIniPairs values;
    u8 owned; // set to 1 if data should be automatically MFree()'d when MIniFree() is called
} MIni;

i32 MIniLoadFile(MIni *ini, const char *filePath);

i32 MIniParse(MIni *ini);

i32 MIniFree(MIni *ini);

i32 MIniReadI32(MIni *ini, const char *var, i32 *valOut);

typedef struct {
    void *fileHandle;
} MIniSave;

i32 MIniMWriteI32(MIniSave *ini, const char *key, i32 value);

i32 MIniSaveFile(MIniSave *ini, const char *filePath);

void MIniSaveFree(MIniSave *ini);

#ifdef __cplusplus
}
#endif
