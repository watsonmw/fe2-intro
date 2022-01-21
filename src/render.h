#ifndef FINTRO_RENDER_H
#define FINTRO_RENDER_H

#include <stdint.h>

#include "mlib.h"
#include "fmath.h"
#include "assets.h"
#include "audio.h"

#define FONT_HEIGHT 9
#define FONT_NEW_LINE 10
#define FONT_MIN_WIDTH 8

// Control screen resolution multipliers.
// '1' is the original resolution of 320x168
// '2' is 2x the original resolution: 640x336
// '3' is 4x the original resolution: 1280x672
#ifndef FINTRO_SCREEN_RES
#define FINTRO_SCREEN_RES 2
#endif

#ifndef SURFACE_WIDTH
#if FINTRO_SCREEN_RES == 1
#define SURFACE_WIDTH 320
#elif FINTRO_SCREEN_RES == 2
#define SURFACE_WIDTH 640
#elif FINTRO_SCREEN_RES == 3
#define SURFACE_WIDTH 1280
#endif
#endif

#ifndef SURFACE_HEIGHT
#if FINTRO_SCREEN_RES == 1
#define SURFACE_HEIGHT 168
#elif FINTRO_SCREEN_RES == 2
#define SURFACE_HEIGHT 400
#elif FINTRO_SCREEN_RES == 3
#define SURFACE_HEIGHT 672
#endif
#endif

// Maximums for complex poly fillers.
#define COMPLEX_MAX_SPANS 0x10
#define BODY_MAX_SPANS 0x20

#ifdef __cplusplus
extern "C" {
#endif

// 256 colour surface for drawing into
typedef struct sSurface {
    u16 width;
    u16 height;
    u8* pixels;
#ifdef FINSPECTOR
    u32* insOffset;
    u32 insOffsetTmp;
#endif
} Surface;

typedef struct sRGB {
    u8 r;
    u8 g;
    u8 b;
} RGB;

void Surface_Init(Surface* surface, u16 width, u16 height);
void Surface_Free(Surface* surface);

void Surface_Clear(Surface* surface, u8 colour);
void Surface_DrawLine(Surface* surface, int x1, int y1, int x2, int y2, u8 colour);
void Surface_DrawLineClipped(Surface* surface, int x1, int y1, int x2, int y2, u8 colour);
void Surface_DrawTriFill(Surface* surface, Vec2i16 points[3], u8 colour);
void Surface_DrawQuadFill(Surface* surface, Vec2i16 points[4], u8 colour);
void Surface_DrawCircleFill(Surface* surface, int x, int y, int diameter, u8 colour);
void Surface_DrawFlare(Surface* surface, i16 x, i16 y, int diameter, u16 colour1, u16 colour2);
void Surface_DrawRect(Surface* surface, int x1, int y1, int x2, int y2, u8 colour);
void Surface_DrawRectFill(Surface* surface, int x1, int y1, int x2, int y2, u8 colour);

MARRAY_TYPEDEF(u8*, PtrsU8)

typedef struct sZTree {
    u8* data;
    u8* root;
    u32 offset;
    u32 size;
    u32 firstNodeOffset;

#ifdef FINSPECTOR
    u32 insOffsetTmp;
    u32* insOffset;
#endif

    PtrsU8 subTrees;
} ZTree;

enum PaletteEntryStateEnum {
    PALETTESTATE_FIXED = 0,          // Pre allocated fixed colour
    PALETTESTATE_MATCHED = 1,        // Matched last frame, but we would prefer a better / closer matched colour
    PALETTESTATE_REQUEST = 2,        // Apply for colour
    PALETTESTATE_FREE = 3,           // Available
    PALETTESTATE_REFD_NON_FRESH = 4, // Was used in a previous frame
    PALETTESTATE_REFD_FRESH = 6,     // Used in this frame
};

typedef struct sVirtualPalette {
    u8 state;
    u8 index;
    u16 colourOf4096;
} MSTRUCTPACKED VirtualPalette;

typedef struct sUpdateColour {
    u8 index;
    u16 colour;
} UpdateColour;

#define PALETTE_3D_COLOURS 0x80
#define PALETTE_VIRTUAL_COLOURS 0x100

typedef struct sPaletteContext {
    // Current set of colours
    VirtualPalette virtualPalette[PALETTE_VIRTUAL_COLOURS];

    // 12bit colour lookup table, contains index into palette if currently allocated or 0xff if not used
    u8 allColours[4096];

    // Free colour index list
    u8 freeColours[PALETTE_VIRTUAL_COLOURS];
    u8 nextFreeColour;

    u16 backgroundColour;
    u16 lastBackgroundColour;

    // Number of new / updated colours this frame
    u16 updatedColoursNum;

    // New / updated colours this frame
    UpdateColour updateColours[PALETTE_VIRTUAL_COLOURS];
} PaletteContext;

typedef struct sSpanLine {
    u16 num;
    i16 span[COMPLEX_MAX_SPANS];
} SpanLine;

typedef struct sSpanRenderer {
    i16 spanStart;
    i16 spanEnd;
    u16 height;
    SpanLine* spans;
    u32 memSize;
} SpanRenderer;

typedef struct sSpan {
    i16 x;
    u16 colour;
} Span;

typedef struct sBodySpan {
    u16 num;
    Span s[BODY_MAX_SPANS];
} BodySpan;

typedef struct sBodySpanRenderer {
    i16 spanStart;
    i16 spanEnd;
    u16 maxSpan;
    u16 maxSpansPerLine;
    u16 height;

    u16 numColours;
    u16 colours[16];

    BodySpan* spans;
    u16* rowBeginColour;
} BodySpanRenderer;

typedef struct sRasterFunc {
    u16 func;    // func id
    u8 params[]; // list of params to func (size depends on rasterop)
} MSTRUCTPACKED DrawFunc;

typedef struct sRasterOpNode {
    u32 z;           // top node has z of 0, sort order
    u16 leftOffset;  // left node or zero if no node
    u16 rightOffset; // left node or zero if no node
    DrawFunc func;   // draw func
} MSTRUCTPACKED RasterOpNode; // packed so we can read raw memdumps

MARRAY_TYPEDEF(RasterOpNode*, RasterOpNodeArray)

typedef struct sRasterContext {
    Surface* surface;

    PaletteContext paletteContext;
    ZTree zTree;
    SpanRenderer spanRenderer;
    BodySpanRenderer bodySpanRenderer;
    RasterOpNodeArray drawNodeStack;

    u16 legacy; // set to 1 to enable legacy mode
} RasterContext;

void Raster_Init(RasterContext* raster);
void Raster_Free(RasterContext* raster);
void Raster_Draw(RasterContext* raster);

void Palette_SetupForNewFrame(PaletteContext* context, b32 resetAll);
void Palette_CalcDynamicColourUpdates(PaletteContext* context);
void Palette_CopyDynamicColoursRGB(PaletteContext* context, RGB* palette);
void Palette_CopyFixedColoursRGB(PaletteContext* context, RGB* palette);
void Palette_CopyAllColoursRGB(PaletteContext* context, RGB* palette);
void Palette_CopyDynamicColours16(PaletteContext* context, u16* palette);
void Palette_CopyFixedColours16(PaletteContext* context, u16* palette);

typedef struct MMemStack {
    u8* mem;
    u8* pos;
    size_t size;
} MMemStack;

typedef struct sSceneSetup {
    // Viewspace
    Matrix3x3i16 viewMatrix;

    u16 x12;

    // Viewport vec to object
    Vec3i32 viewVec;

    // Model to render (this model can render sub models)
    u16 modelIndex;

    // Model variables used by model code
    u16 modelVars[0x80];

    i16 renderPlanetAtmos;

    // Light vec transformed into viewspace
    Vec3i16 lightVec;

    // Shading map, added to model base colours based on facing towards light
    i16 shadeRamp[8];

    // Random seed vars, mutated everytime a new random is generated
    u32 random1;
    u32 random2;

    i16 zScale;

    // Model text (e.g. ship name / base name / platform name)
    i8 modelText[40];

    // List of strings available for rendering
    u8** moduleStrings;

    AssetsData assets;

    u8 bitmapFontColours[16];

    RasterContext* raster;
    AudioContext* audio;

    MMemStack memStack;
#ifdef FINSPECTOR
    // Logging info
    int logLevel;
    u32Array loadedModelIndexes;
    ByteCodeTraceArray byteCodeTrace;
    b32 hideModel[200];
    u8* modelDataFileStartAddress;
    u8* galmapModelDataFileStartAddress;
    u8* fontModelDataFileStartAddress;
    u32 renderDataOffset;
#endif
} SceneSetup;

// Model renderer
void Render_Init(SceneSetup* sceneSetup, RasterContext* raster);
void Render_Free(SceneSetup* sceneSetup);
void Render_RenderScene(SceneSetup* sceneSetup);
void Render_RenderAndDrawScene(SceneSetup* sceneSetup, b32 resetPalette);

static ModelData* Render_GetModel(SceneSetup* sceneSetup, u16 offset) {
    return MArrayGet(sceneSetup->assets.models, offset);
}

#ifdef FINSPECTOR
static u32 Render_GetModelCodeOffset(SceneSetup* sceneSetup, u16 offset) {
    ModelData* modelData = MArrayGet(sceneSetup->assets.models, offset);
    if (modelData == NULL) {
        return 0;
    }

    u32 fileOffset = ((u8*)modelData - sceneSetup->modelDataFileStartAddress);
    return fileOffset + modelData->codeOffset;
}
#endif

// String loading and formatting
u32 Render_LoadFormattedString(SceneSetup* sceneSetup, u16 index, i8* outputBuffer, u32 outputBufferLen);
u32 Render_ProcessString(SceneSetup* sceneSetup, const i8* text, i8* outputBuffer, u32 outputBufferLen);
void Render_DrawBitmapText(SceneSetup* sceneSetup, const i8* text, Vec2i16 pos, u8 colour, b32 drawShadow);

// Image / Bitmap functions
typedef struct {
    u16 w;
    u16 h;

    u8* data;
} Image8Bit;

MARRAY_TYPEDEF(Image8Bit, Images8Bit)

typedef struct sImageStore {
    Images8Bit images;
} ImageStore;

void Render_BlitNoClip(Surface* surface, Image8Bit* image, Vec2i16 pos);
void Render_ImageFromPlanerBitmap(Image8Bit* image, const u8* bitmapRaw, const u16* colours, u16 numColours);
void Render_ImageUpscale2x(Image8Bit* srcImage, Image8Bit* destImage);

// Math functions
enum RotateAxisEnum {
    RotateAxis_X = 0,
    RotateAxis_Y = 1,
    RotateAxis_Z = 2,
};

#ifdef FINSPECTOR
static u32 Render_GetModelCodeOffset(SceneSetup* sceneSetup, u16 offset) {
    ModelData* modelData = MArrayGet(sceneSetup->assets.models, offset);
    if (modelData == NULL) {
        return 0;
    }

    u32 fileOffset = ((u8*)modelData - sceneSetup->modelDataFileStartAddress);
    return fileOffset + modelData->codeOffset;
}
#endif

void Render_RunTests(void);

#ifdef __cplusplus
}
#endif

#endif
