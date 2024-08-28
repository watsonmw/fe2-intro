#ifndef FINTRO_BUILDDATA_H
#define FINTRO_BUILDDATA_H

#include "mlib.h"

#ifdef __cplusplus
extern "C" {
#endif

i32 Build_Stars(const char* filepath, u8* out, i32 images, i32 tileSize, i32 tileSize2);

#ifdef __cplusplus
}
#endif

#endif
