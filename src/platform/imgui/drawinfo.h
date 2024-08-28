#ifndef FINTRO_DRAWINFO_H
#define FINTRO_DRAWINFO_H

#include <stdint.h>

#include "mlib.h"
#include "mcpplib.h"
#include "fmath.h"
#include "assets.h"
#include "render.h"

typedef struct sDebugDrawInfo {
    int batchesDrawn;
} DebugDrawInfo;

void DumpDrawInfo(RasterContext* raster, DebugDrawInfo& drawInfo, MStrWriter* writer);

typedef struct sDebugPaletteInfo {
    MStrWriter writer;
} DebugPaletteInfo;

void DumpPaletteInfo(RasterContext* raster, RGB* screenPalette, DebugPaletteInfo& paletteInfo);

#endif
