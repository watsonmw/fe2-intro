#ifndef FINTRO_RENDER_INTERNAL_H
#define FINTRO_RENDER_INTERNAL_H

#include <stdint.h>

#include "mlib.h"
#include "fmath.h"

typedef struct sModelData {
    u16 codeOffset;   // 0
    u16 vertexDataOffset; // 2
    u16 verticesDataSize; // 4 - (* (64 - some size)
    u16 normalsOffset;    // 6
    u16 normalDataSize;   // 8 - ((* 4) + 4)
    u16 scale1;  // a
    u16 scale2;  // c
    u16 radius;  // e
    u16 x10;  // 10
    u16 colour; // 12
    u16 x14;  // 14
    u16 x16;  // 16
    u16 x18;  // 18
    u16 x1a;  // 1a
    u16 x1c;  // 1c
} MSTRUCTPACKED ModelData;

typedef struct sFontModelData {
    u16 byteCodeOffset;   // 0
    u16 vertexDataOffset; // 2
    u16 verticesDataSize; // 4 - (* (64 - some size)
    u16 normalsOffset;    // 6
    u16 normalDataSize;   // 8 - ((* 4) + 4)
    u16 scale1;  // a
    u16 scale2;  // c
    u16 radius;  // e
    u16 x10;  // 10
    u16 colour; // 12
    u16 x14;  // 14
    u16 x16;  // 16
    u16 x18;  // 18
    u16 x1a;  // 1a
    u16 x1c;  // 1c
    u16 newLineVector;  // 1e
    u16 offsets[];
} MSTRUCTPACKED FontModelData;

typedef enum eRSpanFunc {
    RComplexFunc_Done = 0x0,
    RComplexFunc_Bezier = 0x1,
    RComplexFunc_Line = 0x2,
    RComplexFunc_LineCont = 0x3,
    RComplexFunc_BezierCont = 0x4,
    RComplexFunc_LineJoin = 0x5,
    RComplexFunc_Circle = 0x6
} RSpanFunc;

typedef enum eRenderEnum {
    Render_DONE = 0x0,
    Render_CIRCLE = 0x1,
    Render_LINE = 0x2,
    Render_TRI = 0x3,
    Render_QUAD = 0x4,
    Render_COMPLEX = 0x5,
    Render_BATCH = 0x6,
    Render_MIRRORED_TRI = 0x7,
    Render_MIRRORED_QUAD = 0x8,
    Render_TEARDROP = 0x9,
    Render_VTEXT = 0xa,
    Render_IF = 0xb,
    Render_IF_NOT = 0xc,
    Render_CALC_A = 0xd,
    Render_MODEL = 0xe,
    Render_AUDIO_CUE = 0xf,
    Render_CONE = 0x10,
    Render_CONE_COLOUR_CAP = 0x11,
    Render_BITMAP_TEXT = 0x12,
    Render_IF_NOT_VAR = 0x13,
    Render_IF_VAR = 0x14,
    Render_CLEARZ = 0x15,
    Render_LINE_BEZIER = 0x16,
    Render_IF_VIEWSPACE_DIST = 0x17,
    Render_BALLS = 0x18,
    Render_MATRIX_SETUP = 0x19,
    Render_COLOUR = 0x1a,
    Render_MODEL_SCALE = 0x1b,
    Render_MATRIX_TRANSFORM = 0x1c,
    Render_CALC_B = 0x1d,
    Render_MATRIX_COPY = 0x1e,
    Render_PLANET = 0x1f
} RenderEnum;

typedef enum eMathFuncEnum {
    MathFunc_Add = 0,
    MathFunc_Sub = 1,
    MathFunc_Mult = 2,
    MathFunc_Div = 3,
    MathFunc_DivPower2 = 4,
    MathFunc_MultPower2 = 5,
    MathFunc_Max = 6,
    MathFunc_Min = 7,
    MathFunc_Mult2 = 8,
    MathFunc_DivPower2Signed = 9,
    MathFunc_RGetIndirectOffset = 10,
    MathFunc_ZeroIfGreater = 11,
    MathFunc_ZeroIfLess = 12,
    MathFunc_MultSine = 13,
    MathFunc_MultCos = 14,
    MathFunc_And = 15,
} MathFuncEnum;

typedef enum eDrawFuncEnum {
    DRAW_FUNC_NULL = 0,
    DRAW_FUNC_SPANS_START = 1,
    DRAW_FUNC_SPANS_BEZIER = 2,
    DRAW_FUNC_SPANS_DRAW = 3,
    DRAW_FUNC_SPANS_LINE = 4,
    DRAW_FUNC_SPANS_LINE_CONT = 5,
    DRAW_FUNC_SPANS_POINT = 6,
    DRAW_FUNC_SPANS_END = 7,
    DRAW_FUNC_BATCH_START = 8,
    DRAW_FUNC_BATCH_END1 = 9,
    DRAW_FUNC_BATCH_END = 10,
    DRAW_FUNC_LINE = 11,
    DRAW_FUNC_TRI = 12,
    DRAW_FUNC_QUAD = 13,
    DRAW_FUNC_BEZIER_LINE = 14,
    DRAW_FUNC_FLARE = 15,
    DRAW_FUNC_CIRCLE = 16,
    DRAW_FUNC_RINGED_CIRCLE = 17,
    DRAW_FUNC_BALLS = 18,
    DRAW_FUNC_TEXT = 19,
    DRAW_FUNC_SUBTREE = 20,
    DRAW_FUNC_BODY_START = 21,
    DRAW_FUNC_BODY_LINE = 22,
    DRAW_FUNC_BODY_BEZIER = 23,
    DRAW_FUNC_BODY_XOR = 24,
    DRAW_FUNC_BODY_DRAW_1 = 25,
    DRAW_FUNC_BODY_DRAW_2 = 26,
    DRAW_FUNC_BODY_DRAW_3 = 27,
    DRAW_FUNC_NOP = 28
} DrawFuncEnum;

typedef struct sVertexData {
    Vec2i16 sVec; // vertex in screen coords

    Vec3i32 vVec; // vertex in view space
    Vec3i32 wVec; // vertex in world space

    // -1    : Transformed only - directly
    //  0    : Not projected yet
    //  1    : Transformed only - indirectly
    //  2(+) : Fully transformed & projected to frame with id 2(+)
    i16 projectedState;
} VertexData;

typedef struct sDrawParamsLine {
    i16 x1;
    i16 y1;
    i16 x2;
    i16 y2;
} MSTRUCTPACKED DrawParamsLine;

typedef struct sDrawParamsPoint {
    i16 x;
    i16 y;
} MSTRUCTPACKED DrawParamsPoint;

typedef struct sDrawParamsBezier {
    // 0 - start point
    // 1 - control point 1
    // 2 - control point 2
    // 3 - end point
    Vec2i16 pts[4];
} MSTRUCTPACKED DrawParamsBezier;

typedef struct sDrawParamsLineColour {
    i16 x1;
    i16 y1;
    i16 x2;
    i16 y2;
    i16 colour;
} MSTRUCTPACKED DrawParamsLineColour;

typedef struct sDrawParamsTri {
    Vec2i16 points[3];
    i16 colour;
} MSTRUCTPACKED DrawParamsTri;

typedef struct sDrawParamsQuad {
    Vec2i16 points[4];
    i16 colour;
} MSTRUCTPACKED DrawParamsQuad;

typedef struct sDrawParamsFlare {
    u16 innerColour;
    u16 x;
    u16 y;
    i16 diameter; // diameters smaller than 0 are fractional
    u16 outerColour;
} MSTRUCTPACKED DrawParamsFlare;

typedef struct sDrawParamsColour {
    u16 colour;
} MSTRUCTPACKED DrawParamsColour;

typedef struct sDrawParamsCircle {
    u16 colour;
    i16 x;
    i16 y;
    u16 diameter;
} MSTRUCTPACKED DrawParamsCircle;

typedef struct sDrawParamsLegacyBalls {
    u16 width;
    u16 colour;
    u16 pos[];
}  MSTRUCTPACKED DrawParamsLegacyBalls;

typedef struct sDrawParamsBalls {
    u16 width;
    u16 colour;
    u16 num;
    u16 pos[];
}  MSTRUCTPACKED DrawParamsBalls;

typedef struct sDrawParamsRingedCircle {
    u16 innerColour;
    i16 x;
    i16 y;
    i16 diameter;
    u16 outerColour;
} MSTRUCTPACKED DrawParamsRingedCircle;

typedef struct sDrawParamsBezierColour {
    // 0 - start point
    // 1 - control point 1
    // 2 - control point 2
    // 3 - end point
    Vec2i16 pts[4];
    u16 colour;
} MSTRUCTPACKED DrawParamsBezierColour;

typedef struct sDrawParamsText {
    u16 strOffset;
    i16 x;
    i16 y;
    u16 t1;
    u16 t2;
    u16 t3;
}  MSTRUCTPACKED DrawParamsText;

typedef struct sDrawParamsBodyXor {
    i16 offset;
    i16 colour;
}  MSTRUCTPACKED DrawParamsBodyXor;

typedef struct sDrawParamsColour8 {
    u16 colour[8];
}  MSTRUCTPACKED DrawParamsColour8;

typedef struct sDrawParamsColour16 {
    u16 colour[16];
}  MSTRUCTPACKED DrawParamsColour16;

// grab low byte from word and sign extend
static inline i16 lo8s(u16 val) {
    return (i16)((i8)(val & 0xff));
}

// grab hi byte from word and sign extend
static inline i16 hi8s(u16 val) {
    return (i16)((i8)(val >> 8));
}

// grab low byte from word and sign extend
static inline i32 lo8s32(u16 val) {
    return (i32)((i8)(val & 0xff));
}

// grab hi byte from word and sign extend
static inline i32 hi8s32(u16 val) {
    return (i32)((i8)(val >> 8));
}

// swap high and low words
static inline u32 flipWords(u32 v) {
    return (v >> 16 | v << 16);
}

#endif
