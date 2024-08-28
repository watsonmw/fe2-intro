#include "mini.h"
#include "mlib.h"

#ifdef USE_SDL
#include <SDL2/SDL.h>
#endif

MINLINE int M_IsWhitespace(char c) {
    return (c == '\n' || c == '\r' || c == '\t' || c == ' ');
}

MINLINE int M_IsSpaceTab(char c) {
    return (c == '\t' || c == ' ');
}

MINLINE int M_IsNewLine(char c) {
    return (c == '\n' || c == '\r');
}

i32 MIniLoadFile(MIni* ini, const char* fileName) {
    MReadFileRet ret = MFileReadFully(fileName);
    if (ret.size) {
        ini->data = ret.data;
        ini->dataSize = ret.size;
        ini->owned = 1;
        return MIniParse(ini);
    }
    return 0;
}

enum MIniParseState {
    MIniParseState_INIT,
    MIniParseState_READ_KEY,
    MIniParseState_READ_EQUALS,
    MIniParseState_READ_EQUALS_AFTER,
    MIniParseState_IGNORE_LINE,
    MIniParseState_READ_VALUE
};

i32 MIniParse(MIni* ini) {
    MArrayInit(ini->values);

    MIniPair* pair;

    enum MIniParseState state = MIniParseState_INIT;

    char* startKey = 0;
    char* endKey = 0;
    char* startValue = 0;
    char* pos = (char*)ini->data;

    for (int i = 0; i < ini->dataSize; i++) {
        char c = *pos;
        switch (state) {
            case MIniParseState_INIT:
                if (!M_IsWhitespace(c)) {
                    startKey = pos;
                    state = MIniParseState_READ_KEY;
                }
                break;
            case MIniParseState_READ_KEY:
                if (M_IsSpaceTab(c)) {
                    endKey = pos;
                    state = MIniParseState_READ_EQUALS;
                } else if (M_IsNewLine(c)) {
                    state = MIniParseState_INIT;
                } else if (c == '=') {
                    endKey = pos;
                    state = MIniParseState_READ_EQUALS_AFTER;
                }
                break;
            case MIniParseState_READ_EQUALS:
                if (c == '=') {
                    state = MIniParseState_READ_EQUALS_AFTER;
                } else if (!M_IsSpaceTab(c)) {
                    state = MIniParseState_IGNORE_LINE;
                }
                break;
            case MIniParseState_IGNORE_LINE:
                if (M_IsNewLine(c)) {
                    state = MIniParseState_INIT;
                }
                break;
            case MIniParseState_READ_EQUALS_AFTER:
                if (M_IsNewLine(c)) {
                    state = MIniParseState_INIT;
                } else if (!M_IsSpaceTab(c)) {
                    startValue = pos;
                    state = MIniParseState_READ_VALUE;
                }
                break;
            case MIniParseState_READ_VALUE:
                if (M_IsWhitespace(c)) {
                    pair = MArrayAddPtr(ini->values);
                    pair->key = startKey;
                    pair->keySize = endKey - startKey;
                    pair->val = startValue;
                    pair->valSize = pos - startValue;
                    state = MIniParseState_INIT;
                }
                break;
        }
        pos++;
    }

    return 1;
}

i32 MIniFree(MIni* ini) {
    MArrayFree(ini->values);

    if (ini->owned) {
        MFree(ini->data, ini->dataSize); ini->data = 0;
        ini->owned = 0;
    }

    return -1;
}

i32 MIniReadI32(MIni* ini, const char* key, i32* valOut) {
    u32 inSize = strlen(key);

    for (int i = 0; i < MArraySize(ini->values); i++) {
        MIniPair* pair = MArrayGetPtr(ini->values, i);

        if (inSize == pair->keySize) {
            int notEqual = 0;
            for (int j = 0; j < pair->keySize; j++) {
                if (pair->key[j] != key[j]) {
                    notEqual = 1;
                    break;
                }
            }

            if (!notEqual) {
                return MParseI32(pair->val, pair->val + pair->valSize, valOut);
            }
        }
    }

    return -1;
}

i32 MIniSaveFile(MIniSave* ini, const char* filePath) {
    ini->fileHandle = 0;

#ifdef USE_SDL
    SDL_RWops *file = SDL_RWFromFile(filePath, "w");
    if (file == NULL) {
        return -1;
    }

    ini->fileHandle = file;
#endif

    return 0;
}

i32 MIniMWriteI32(MIniSave* ini, const char* key, i32 value) {
#ifdef USE_SDL
    SDL_RWops *file = (SDL_RWops*)ini->fileHandle;

    const int bufferSize = 1024;
    char buffer[bufferSize];

    int n = snprintf(buffer, bufferSize, "%s = %d\n", key, value);
    SDL_RWwrite(file, buffer, n, 1);
#endif

    return 0;
}

void MIniSaveFree(MIniSave* ini) {
    if (ini->fileHandle) {
#ifdef USE_SDL
        SDL_RWclose((SDL_RWops*)ini->fileHandle);
#endif
    }
    ini->fileHandle = 0;
}
