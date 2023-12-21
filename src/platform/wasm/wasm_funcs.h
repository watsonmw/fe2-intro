#pragma once

#include "mlib.h"

// Imports from Javascript
void WASM_Print(const char *str, size_t len);
void WASM_PrintLine(const char *str, size_t len);
u32 WASM_FileGetSize(const char* filePathBuf, u32 filePathSize);
size_t WASM_FileRead(const char* filePathBuf, u32 filePathSize, u8* dest, u32 offset, u32 readSize);
void WASM_AudioPause(void);
void WASM_AudioResume(void);
