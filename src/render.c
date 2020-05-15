#include "render.h"
#include "renderinternal.h"
#include "mlib.h"
#include "fmath.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#if FINTRO_SCREEN_RES == 1
#define SCREEN_SCALE 1
#define ZCLIPNEAR 0x40
#define ZSCALE 8
#define STAR_SCALE 0
#define FONT_SCALE SCREEN_SCALE
#define TEARDROP_SCALE SCREEN_SCALE
#define HIGHLIGHTS_SMALL 7
#define HIGHLIGHTS_BIG 9
#define HIGHLIGHTS_SIZE 16
#define PLANET_SCREENSPACE_MAX_DIST 0xb4
#define PLANET_SCREENSPACE_MAX_DIST2 0x600
#define PLANET_SCREENSPACE_MIN_SCATTER 0x199
#elif FINTRO_SCREEN_RES == 2
#define SCREEN_SCALE 2
#define ZCLIPNEAR 0x80
#define ZSCALE 9
#define STAR_SCALE (SCREEN_SCALE - 1)
#define FONT_SCALE SCREEN_SCALE
#define TEARDROP_SCALE SCREEN_SCALE
#define HIGHLIGHTS_SMALL 9
#define HIGHLIGHTS_BIG 22
#define HIGHLIGHTS_SIZE 32
#define PLANET_SCREENSPACE_MAX_DIST 0x168
#define PLANET_SCREENSPACE_MAX_DIST2 0xb00
#define PLANET_SCREENSPACE_MIN_SCATTER 0x332
#elif FINTRO_SCREEN_RES == 3
#define SCREEN_SCALE 4
#define ZCLIPNEAR 0x100
#define ZSCALE 10
#define STAR_SCALE (SCREEN_SCALE - 1)
#define FONT_SCALE SCREEN_SCALE
#define TEARDROP_SCALE 3
#define HIGHLIGHTS_SMALL 9
#define HIGHLIGHTS_BIG 22
#define HIGHLIGHTS_SIZE 32
#define PLANET_SCREENSPACE_MAX_DIST 0x2d0
#define PLANET_SCREENSPACE_MAX_DIST2 0x1600
#define PLANET_SCREENSPACE_MIN_SCATTER 0x664
#endif

#define PLANET_CLIP_BORDER (SURFACE_HEIGHT/2)
#define BEZIER_STEP_3_LEN 8
#define BEZIER_STEP_7_LEN 50
#define BEZIER_STEP_15_LEN 300

#define BACKGROUND_COLOUR_INDEX 0x7f

#ifdef FINSPECTOR
#include "builddata.h"
#endif

#define PT_ZCLIPPED ((i16)0x8002) // Special x & y value indicating a raster point is z clipped
#define NORMAL_NOT_CALCULATED ((u16)0x8080)
#define NORMAL_FACING_AWAY ((u16)0x8000)

#define MINTERNAL static

// try out different code for :
// - ZProject
// - Span - AddLine / Clipping
// - DrawSpan write

// document planet rendering

#ifdef __GNUC__
// Slightly faster than malloc/alloc solutions in GCC 6.3
#define FRAME_MEM_USE_STACK_DARY 1
#else
// Most compatible / less stack usage, uses separate stack
#define FRAME_MEM_USE_MALLOC 1
// uncomment the following to use alloc instead
//#define FRAME_MEM_USE_STACK_ALLOC 1
#endif

// Custom stack based mem allocator, so we don't pollute/consume process/thread stack
MINTERNAL void MMemStackInit(MMemStack* memStack, size_t size) {
    memStack->mem = MMalloc(size);
    memStack->pos = memStack->mem;
    memStack->size = size;
}

MINTERNAL void MMemStackFree(MMemStack* memStack) {
    MFree(memStack->mem);
}

MINTERNAL u8* MMemStackAlloc(MMemStack* memStack, size_t size) {
    u8* pos = memStack->pos;
    memStack->pos = pos + size;
    return pos;
}

MINTERNAL void MMemStackReset(MMemStack* memStack, void* resetPos) {
    memStack->pos = (u8*)resetPos;
}

// Clip in y axis (useful for span renderers)
MINTERNAL b32 ClipLineY(i16* restrict x1, i16* restrict y1, i16* restrict x2, i16* restrict y2, i16 h) {
    u16 clip1 = 0;
    u16 clip2 = 0;
    if (*y1 > h) {
        clip1 = 0x1;
    } else if (*y1 < 0) {
        clip1 = 0x2;
    }

    if (*y2 > h) {
        clip2 = 0x1;
    } else if (*y2 < 0) {
        clip2 = 0x2;
    }

    if (clip1 == clip2) {
        return clip1 == 0;
    }

    i32 xLen = *x2 - *x1;
    i32 yLen = *y2 - *y1;

    if (clip1 == 0x1) {
        *x1 = *x1 + (xLen * ((i32)h - (i32)*y1)) / yLen;
        *y1 = h;
    } else if (clip1 == 0x2) {
        *x1 = *x1 + (xLen * (0 - (i32)*y1)) / yLen;
        *y1 = 0;
    }

    xLen = -xLen;
    yLen = -yLen;

    if (clip2 == 0x1) {
        i32 val2 = (xLen * ((i32)h - ((i32)*y2)));
        *x2 = *x2 + val2 / yLen;
        *y2 = h;
    } else if (clip2 == 0x2) {
        *x2 = *x2 + (xLen * (0 - (i32)*y2)) / yLen;
        *y2 = 0;
    }

    return TRUE;
}

MINTERNAL u8 GetClipBits(Vec2i16* p, u16 w, u16 h) {
    u8 clipBits = 0;
    if (p->x > w) {
        clipBits |= (u8)0x2;
    } else if (p->x < 0) {
        clipBits |= (u8)0x1;
    }
    if (p->y > h) {
        clipBits |= (u8)0x8;
    } else  if (p->y < 0) {
        clipBits |= (u8)0x4;
    }
    return clipBits;
}

MINTERNAL b32 ClipLinePoint(Vec2i16* restrict p1, Vec2i16* restrict p2, u16 w, u16 h, u8 clip) {
    i16 xLen = p2->x - p1->x;
    i16 yLen = p2->y - p1->y;

    Vec2i16 r;

    if (clip & 0x1) {
        // point is left of clip plane
        r.y = p1->y + (yLen * (0 - p1->x)) / xLen;
        r.x = 0;
        if (r.y < 0) {
            clip = 0x4;
        } else if (r.y > h) {
            clip = 0x8;
        } else {
            goto done;
        }
    } else if (clip & 0x2) {
        // point is right of clip plane
        r.y = p1->y + (yLen * (w - p1->x)) / xLen;
        r.x = w;
        if (r.y < 0) {
            clip = 0x4;
        } else if (r.y > h) {
            clip = 0x8;
        } else {
            goto done;
        }
    }

    if (clip == 0x4) {
        // point is above the clip plane
        r.x = p1->x + (xLen * (0 - p1->y)) / yLen;
        r.y = 0;
        if (r.x < 0 || r.x >= w) {
            return FALSE;
        }
    } else if (clip == 0x8) {
        // point is below the clip plane
        r.x = p1->x + (xLen * (h - p1->y)) / yLen;
        r.y = h;
        if (r.x < 0 || r.x >= w) {
            return FALSE;
        }
    }

    done:
    p1->x = r.x;
    p1->y = r.y;

    return TRUE;
}

MINTERNAL b32 ClipLinePoints(Vec2i16* restrict p1, Vec2i16* restrict p2, u16 w, u16 h) {
    u8 clip1 = GetClipBits(p1, w, h);
    u8 clip2 = GetClipBits(p2, w, h);

    if (clip1 == 0 && clip2 == 0) {
        return TRUE;
    }

    if (clip1 & clip2) {
        return FALSE;
    }

    if (clip1) {
        if (clip2) {
            Vec2i16 p1Temp = *p1;
            return ClipLinePoint(p1, p2, w, h, clip1) && ClipLinePoint(p2, &p1Temp, w, h, clip2);
        } else {
            return ClipLinePoint(p1, p2, w, h, clip1);
        }
    } else if (clip2) {
        return ClipLinePoint(p2, p1, w, h, clip2);
    }

    return TRUE;
}

// Set to 1 when clip code means poly/points is for sure fully outside viewport
static u8 sClipLookupTable[] = {
        0, 1, 0, 0, 1, 0, 0, 0,
        1, 0, 0, 0, 0, 0, 0, 0,
};

// 0x8 z-clipped
// 0x4 greater
// 0x2 inside
// 0x1 less
MINTERNAL u8 UpdateClipBits(i16 x, u8 clipBits, u16 w) {
    if (x == PT_ZCLIPPED) {
        clipBits |= (u8)0x8;
    } else if ((u16)x < w) {
        clipBits |= (u8)0x2;
    } else if (x < 0) {
        clipBits |= (u8)0x1;
    } else {
        clipBits |= (u8)0x4;
    }
    return clipBits;
}

static u16 sRingedCircleDiametersInner[] = {0, 0, 2, 2, 3, 4, 6, 7, 8, 9 };
static u16 sRingedCircleDiametersOuter[] = {0, 2, 3, 5, 5, 6, 7, 9, 10, 11 };

void Surface_Init(Surface* surface, u16 width, u16 height) {
    Surface_Free(surface);
    surface->width = width;
    surface->height = height;
    surface->pixels = (u8*)MMalloc(sizeof(u8) * width * height);
#ifdef FINSPECTOR
    surface->insOffset = (u32*)MMalloc(sizeof(u32) * width * height);
#endif
}

void Surface_Free(Surface* surface) {
    MFree(surface->pixels); surface->pixels = 0;
#ifdef FINSPECTOR
    MFree(surface->insOffset); surface->insOffset = 0;
#endif
}

void Surface_Clear(Surface* surface, u8 colour) {
#ifdef FINSPECTOR
    surface->insOffsetTmp = 0;
    int d = surface->width * surface->height;
    for (int i = 0; i < d; i++) {
        surface->pixels[i] = colour;
        surface->insOffset[i] = 0;
    }
#else
    // Clear 4 bytes at a time
    u32* pixels32 = (u32*)surface->pixels;
    u32 colour32 = colour;
    u32* end = pixels32 + ((SURFACE_HEIGHT * SURFACE_WIDTH) / 4);
    colour32 = (colour32 << 24) + (colour32 << 16) + (colour32 << 8) + colour32;
    for (; pixels32 < end; pixels32++) {
        *(pixels32) = colour32;
    }
#endif
}

// tests
// - writing words is better than bytes?
static void DrawSpanNoClip(u8* restrict pixelsLine, i16 x1, i16 x2, u8 colour) {
#ifdef AMIGA
#if defined(__GNUC__)
    u8* restrict dummy1;
    i16 dummy2;
    __asm__ volatile (
        "sub.w %3, %4\n\t"
        "ble .done%=\n\t"
        "lea (%2,%3.w), %0\n\t"
        "bra .dobra%=\n\t"
        ".loop%=: move.b %5,(%0)+\n\t"
        ".dobra%=: dbra.w %4,.loop%=\n\t"
        ".done%=:\n\t"
       : "=a"(dummy1), "=d"(dummy2)
       : "a"(pixelsLine), "d"(x1), "1"(x2), "d"(colour)

       // dummy1 dummy2
       // 0      1
       // pixelsLine x1 x2 colour
       // 2          3  4  5
    );
#elif defined(__VBCC__)
    for (; x1 < x2; x1++) {
        *(pixelsLine + x1) = colour;
    }
#endif
#else
    for (; x1 < x2; x1++) {
        *(pixelsLine + x1) = colour;
    }
#endif
}

MINTERNAL void DrawSpanClipped(Surface* restrict surface, int x1, int y, int x2, u8 colour) {
    if (y < 0 || y >= SURFACE_HEIGHT) {
        return;
    }

    if (x1 < 0) {
        x1 = 0;
    }

    if (x2 >= SURFACE_WIDTH) {
        x2 = SURFACE_WIDTH - 1;
    }

    int d = (y * SURFACE_WIDTH);

#ifdef FINSPECTOR
    for (; x1 <= x2; ++x1) {
        surface->pixels[d + x1] = colour;
        surface->insOffset[d + x1] = surface->insOffsetTmp;
    }
#else
    DrawSpanNoClip(surface->pixels + d, x1, x2, colour);
#endif
}

MINTERNAL void DrawPixel(Surface* surface, int x, int y, u8 colour) {
    if (y < 0 || y >= surface->height || x < 0 || x >= surface->width) {
        return;
    }

    surface->pixels[y*surface->width + x] = colour;
#ifdef FINSPECTOR
    surface->insOffset[y*surface->width + x] = surface->insOffsetTmp;
#endif
}

void Surface_DrawRect(Surface* surface, int x1, int y1, int x2, int y2, u8 colour) {
    if (x1 < 0) {
        x1 = 0;
    }
    if (x2 >= surface->width) {
        x2 = surface->width - 1;
    }

    if (y1 < 0) {
        y1 = 0;
    }
    if (y2 >= surface->height) {
        y2 = surface->height - 1;
    }

    int h = y2 - y1;
    if (h <= 0) {
        return;
    }
    int w = x2 - x1;
    if (w <= 0) {
        return;
    }

    int d1 = x1 + (y1 * surface->width);
    int d2 = x1 + (y2 * surface->width);
    for (int i = 0; i < w + 1; i++) {
        surface->pixels[d1 + i] = colour;
        surface->pixels[d2 + i] = colour;
#ifdef FINSPECTOR
        surface->insOffset[d1 + i] = surface->insOffsetTmp;
        surface->insOffset[d2 + i] = surface->insOffsetTmp;
#endif
    }

    for (int i = 1; i < h; i++) {
        int d = ((i + y1) * surface->width);
        surface->pixels[d + x1] = colour;
        surface->pixels[d + x2] = colour;
#ifdef FINSPECTOR
        surface->insOffset[d + x1] = surface->insOffsetTmp;
        surface->insOffset[d + x2] = surface->insOffsetTmp;
#endif
    }
}

void Surface_DrawRectFill(Surface* surface, int x1, int y1, int x2, int y2, u8 colour) {
    if (x1 < 0) {
        x1 = 0;
    }
    if (x2 >= surface->width) {
        x2 = surface->width - 1;
    }

    if (y1 < 0) {
        y1 = 0;
    }
    if (y2 >= surface->height) {
        y2 = surface->height - 1;
    }

    int h = y2 - y1;
    if (h <= 0) {
        return;
    }
    int w = x2 - x1;
    if (w <= 0) {
        return;
    }

    for (int i = 0; i < h; i++) {
        int d = ((i + y1) * surface->width) + x1;
        for (int j = 0; j < w; j++) {
            surface->pixels[d + j] = colour;
#ifdef FINSPECTOR
            surface->insOffset[d + j] = surface->insOffsetTmp;
#endif
        }
    }
}

void DrawCircleOutline(Surface* surface, int x, int y, int r, u8 colour) {
    int x1 = r;
    int y1 = 0;
    int d = 3 - 2 * r;

    while (x1 >= y1) {
        y1++;

        if (d > 0) {
            x1--;
            d = d + 4 * (y1 - x1) + 10;
        } else {
            d = d + 4 * y1 + 6;
        }

        DrawPixel(surface, x + x1, y + y1, colour);
        DrawPixel(surface, x - x1, y + y1, colour);
        DrawPixel(surface, x + x1, y - y1, colour);
        DrawPixel(surface, x - x1, y - y1, colour);
        DrawPixel(surface, x + y1, y + x1, colour);
        DrawPixel(surface, x - y1, y + x1, colour);
        DrawPixel(surface, x + y1, y - x1, colour);
        DrawPixel(surface, x - y1, y - x1, colour);
    }
}

void DrawSmallCircle(Surface *surface, int x, int y, int d, u8 colour) {
    if (x < 2 || x >= SURFACE_WIDTH - 1 || y < 2 || y >= SURFACE_HEIGHT - 1) {
        return;
    }
    switch (d) {
        case 0:
            DrawSpanClipped(surface, x, y, x + 1, colour);
            break;
        case 1:
            x -= 1;
            DrawSpanClipped(surface, x, y - 1, x + 2, colour);
            DrawSpanClipped(surface, x, y, x + 2, colour);
            break;
        case 2:
            DrawSpanClipped(surface, x, y - 1, x + 1, colour);
            DrawSpanClipped(surface, x - 1, y, x + 2, colour);
            DrawSpanClipped(surface, x, y + 1, x + 1, colour);
            break;
        case 3:
            x -= 1;
            DrawSpanClipped(surface, x, y - 1, x + 3, colour);
            DrawSpanClipped(surface, x, y, x +  3, colour);
            DrawSpanClipped(surface, x, y + 1, x + 3, colour);
            break;
        case 4:
            x -= 1;
            y -= 1;
            DrawSpanClipped(surface, x, y++, x + 2, colour);
            DrawSpanClipped(surface, x - 1, y++, x + 3, colour);
            DrawSpanClipped(surface, x - 1, y++, x + 3, colour);
            DrawSpanClipped(surface, x, y, x + 2, colour);
            break;
        case 5:
            y -= 2;
            DrawSpanClipped(surface, x, y++, x + 1, colour);
            DrawSpanClipped(surface, x - 1, y++, x + 2, colour);
            DrawSpanClipped(surface, x - 2, y++, x + 3, colour);
            DrawSpanClipped(surface, x - 1, y++, x + 2, colour);
            DrawSpanClipped(surface, x, y, x + 1, colour);
            break;
    }
}

void Surface_DrawCircleFill(Surface* surface, int x, int y, int diameter, u8 colour) {
    if (diameter < 0 || diameter > 0x3f0) {
        return;
    }

    if (diameter < 6) {
        DrawSmallCircle(surface, x, y, diameter, colour);
        return;
    }

    int x1 = diameter / 2;
    int y1 = 0;
    int dx = 1 - diameter;
    int dy = 1;
    int errorTerm = 0;

    u8* pixelsLine = surface->pixels;
    while (x1 >= y1) {
        DrawSpanClipped(surface, x - x1, y + y1, x + x1, colour);
        DrawSpanClipped(surface, x - x1, y - y1, x + x1, colour);
        DrawSpanClipped(surface, x - y1, y + x1, x + y1, colour);
        DrawSpanClipped(surface, x - y1, y - x1, x + y1, colour);
        y1++;

        errorTerm += dy;
        dy += 2;

        if (2 * errorTerm + dx > 0) {
            x1--;
            errorTerm += dx;
            dx += 2;
        }
    }
}

void Surface_DrawTriFill(Surface* surface, Vec2i16 points[3], u8 colour) {
    while ((points[0].y > points[1].y) ||
           (points[0].y > points[2].y)) {

        Vec2i16 t = points[0];
        points[0] = points[1];
        points[1] = points[2];
        points[2] = t;
    }

    int incX = 0;
    int incY = 0;

    // Draw top part
    int dx1 = points[1].x - points[0].x;
    int dy1 = points[1].y - points[0].y;

    int dx2 = points[2].x - points[0].x;
    int dy2 = points[2].y - points[0].y;

    int fx1 = (points[0].x << 16);
    int dfx1 = dy1 == 0 ? 0 : ((dx1 << 16) / dy1);

    int fx2 = fx1;
    int dfx2 = dy2 == 0 ? 0 : ((dx2 << 16) / dy2);

    int yPos = points[0].y;

    if (dfx2 < dfx1) {
        MSWAP(dfx1, dfx2, int);
        incX = 1;
    }

    int spansToDraw = dy1;
    if (dy1 > dy2) {
        spansToDraw = dy2;
        incY = 1;
    }

    u8* pixelsLine = surface->pixels + (SURFACE_WIDTH * yPos);
    int xx1, xx2;
    for (; spansToDraw > 0; --spansToDraw) {
        xx1 = fx1 >> 16;
        xx2 = fx2 >> 16;
        DrawSpanNoClip(pixelsLine, xx1, xx2, colour);
        fx1 += dfx1;
        fx2 += dfx2;
        pixelsLine += SURFACE_WIDTH;
    }

    // Draw bottom part
    if (incY) {
        if (incX) {
            dfx1 = dfx2;
            fx1 = fx2;
        }

        int dx = points[1].x - points[2].x;
        int dy = points[1].y - points[2].y;

        dfx2 = dy == 0 ? 0 : ((dx << 16) / dy);
        fx2 = (points[2].x << 16);

        spansToDraw = dy;
    } else {
        if (incX) {
            dfx2 = dfx1;
            fx2 = fx1;
        }

        int dx = points[2].x - points[1].x;
        int dy = points[2].y - points[1].y;

        dfx1 = dy == 0 ? 0 : ((dx << 16) / dy);
        fx1 = (points[1].x << 16);

        spansToDraw = dy;
    }

    if (fx1 > fx2) {
        MSWAP(dfx1, dfx2, int);
        MSWAP(fx1, fx2, int);
    }

    for (; spansToDraw > 0; --spansToDraw) {
        xx1 = fx1 >> 16;
        xx2 = fx2 >> 16;
        DrawSpanNoClip(pixelsLine, xx1, xx2, colour);
        fx1 += dfx1;
        fx2 += dfx2;
        pixelsLine += SURFACE_WIDTH;
    }
}

void Surface_DrawQuadFill(Surface* surface, Vec2i16 points[4], u8 colour) {
    while ((points[0].y > points[1].y) ||
           (points[0].y > points[2].y) ||
           (points[0].y > points[3].y)) {

        Vec2i16 t = points[0];
        points[0] = points[1];
        points[1] = points[2];
        points[2] = points[3];
        points[3] = t;
    }

    int incX = 0;
    int incY = 0;

    // Draw top part
    int dx1 = points[1].x - points[0].x;
    int dy1 = points[1].y - points[0].y;

    int dx2 = points[3].x - points[0].x;
    int dy2 = points[3].y - points[0].y;

    int fx1 = (points[0].x << 16);
    int dfx1 = dy1 == 0 ? 0 : ((dx1 << 16) / dy1);

    int fx2 = fx1;
    int dfx2 = dy2 == 0 ? 0 : ((dx2 << 16) / dy2);

    int yPos = points[0].y;

    if (dfx2 < dfx1) {
        MSWAP(dfx1, dfx2, int);
        incX = 1;
    }

    int spansToDraw = dy1;
    if (dy1 > dy2) {
        spansToDraw = dy2;
        incY = 1;
    }
    dy1 -= spansToDraw;
    dy2 -= spansToDraw;

    u8* pixelsLine = surface->pixels + (SURFACE_WIDTH * yPos);
    int x1,x2;
    for (; spansToDraw > 0; --spansToDraw) {
        x1 = fx1 >> 16;
        x2 = fx2 >> 16;
        DrawSpanNoClip(pixelsLine, x1, x2, colour);
        fx1 += dfx1;
        fx2 += dfx2;
        pixelsLine += SURFACE_WIDTH;
    }

    // Draw mid
    int curr = 0;
    int prev = 3;
    int next = 1;
    if (incY) {
        // still drawing d1
        curr = prev;
        prev -= 1;

        if (incX) {
            dfx1 = dfx2;
            fx1 = fx2;
        }

        int dx = points[prev].x - points[curr].x;
        int dy = points[prev].y - points[curr].y;

        dfx2 = dy == 0 ? 0 : ((dx << 16) / dy);
        fx2 = (points[curr].x << 16);

        dy2 = dy;
    } else {
        // still drawing d2
        curr = next;
        next += 1;

        if (incX) {
            dfx2 = dfx1;
            fx2 = fx1;
        }

        int dx = points[next].x - points[curr].x;
        int dy = points[next].y - points[curr].y;

        dfx1 = dy == 0 ? 0 : ((dx << 16) / dy);
        fx1 = (points[curr].x << 16);

        dy1 = dy;
    }

    spansToDraw = dy1;
    if (dy2 < dy1) {
        spansToDraw = dy2;
        incY = 1;
    } else {
        incY = 0;
    }

    if (fx1 > fx2) {
        MSWAP(dfx1, dfx2, int);
        MSWAP(fx1, fx2, int);
        incX = 1;
    } else {
        incX = 0;
    }

    for (; spansToDraw > 0; --spansToDraw) {
        x1 = fx1 >> 16;
        x2 = fx2 >> 16;
        DrawSpanNoClip(pixelsLine, x1, x2, colour);
        fx1 += dfx1;
        fx2 += dfx2;
        pixelsLine += SURFACE_WIDTH;
    }

    // Draw end
    if (incY) {
        // still drawing d1
        curr = prev;
        prev -= 1;

        if (incX) {
            dfx1 = dfx2;
            fx1 = fx2;
        }

        int dx = points[prev].x - points[curr].x;
        int dy = points[prev].y - points[curr].y;

        dfx2 = dy == 0 ? 0 : ((dx << 16) / dy);
        fx2 = (points[curr].x << 16);

        spansToDraw = dy;
    } else {
        // still drawing d2
        curr = next;
        next += 1;

        if (incX) {
            dfx2 = dfx1;
            fx2 = fx1;
        }

        int dx = points[next].x - points[curr].x;
        int dy = points[next].y - points[curr].y;

        dfx1 = dy == 0 ? 0 : ((dx << 16) / dy);
        fx1 = (points[curr].x << 16);

        spansToDraw = dy;
    }

    if (fx1 > fx2) {
        MSWAP(dfx1, dfx2, int);
        MSWAP(fx1, fx2, int);
    }

    for (; spansToDraw > 0; --spansToDraw) {
        x1 = fx1 >> 16;
        x2 = fx2 >> 16;
        DrawSpanNoClip(pixelsLine, x1, x2, colour);
        fx1 += dfx1;
        fx2 += dfx2;
        pixelsLine += SURFACE_WIDTH;
    }
}

static i8 sDrawHighlightIndex[] = {
#if FINTRO_SCREEN_RES == 1
    -7, -6, -5, -5, -5, -4, -4, -4,
    -4, -3, -3, -3, -3, -2, -2, -2,
    -2, -2, -1, -1, -1, -1, -1, -1,
     0,  0,  0,  0,  0,  0,  0,  0
#else
    -9, -8, -7, -6, -6, -5, -5, -5,
    -4, -4, -4, -3, -3, -3, -3, -2,
    -2, -2, -2, -2, -1, -1, -1, -1,
    -1, -1,  0,  0,  0,  0,  0,  0
#endif
};

// Original resolution flares (16x16)
static u8 sDrawFlareGraphics16[] = {
    // 0
    0, 0, 0, 0, 0, 0, 1, 2,
    0, 0, 0, 0, 0, 0, 0, 1,
    // 1
    0, 0, 0, 0, 0, 1, 1, 3,
    0, 0, 0, 0, 0, 0, 0, 1,
    // 2
    0, 0, 0, 0, 0, 1, 0, 3,
    0, 0, 0, 0, 0, 0, 1, 2,
    // 3
    0, 0, 0, 0, 1, 1, 0, 4,
    0, 0, 0, 0, 0, 0, 1, 2,
    // 4
    0, 0, 0, 0, 1, 1, 2, 4,
    0, 0, 0, 0, 0, 0, 1, 2,
    // 5
    0, 0, 0, 0, 1, 0, 2, 4,
    0, 0, 0, 0, 0, 1, 1, 3,
    // 6
    0, 0, 0, 1, 1, 0, 2, 5,
    0, 0, 0, 0, 0, 1, 1, 3,
    // 19
    0, 0, 0, 1, 1, 0, 0, 5,
    0, 0, 0, 0, 0, 1, 2, 3,
    // 20
    0, 0, 1, 1, 0, 2, 3, 6,
    0, 0, 0, 0, 1, 1, 2, 4,
    // 21
    0, 1, 1, 0, 2, 0, 4, 7,
    0, 0, 0, 1, 1, 2, 3, 5,
    // 22
    1, 1, 0, 0, 2, 3, 4, 8,
    0, 0, 1, 1, 1, 2, 3, 6,
    // 23
    1, 1, 0, 2, 0, 0, 5, 8,
    0, 0, 1, 1, 2, 3, 4, 6,
    // 24
    1, 0, 0, 2, 3, 4, 5, 8,
    0, 1, 1, 1, 2, 3, 4, 6,
    // 25
    1, 0, 2, 0, 0, 0, 6, 8,
    0, 1, 1, 2, 3, 4, 5, 7,
    // 26
    1, 0, 2, 3, 0, 5, 6, 8,
    0, 1, 1, 2, 4, 4, 5, 7,
    // 27
    1, 0, 2, 0, 0, 0, 6, 8,
    0, 1, 1, 3, 4, 5, 5, 7,
    // 28
    1, 0, 0, 4, 5, 0, 0, 8,
    0, 1, 2, 3, 4, 5, 6, 7,
    // 29
    1, 0, 3, 0, 0, 6, 0, 8,
    0, 1, 2, 4, 5, 5, 6, 7,
};

// Double resolution flares (32x32)
static u8 sDrawFlareGraphics32[] = {
    // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    // 1
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,
    // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 3,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2,
    // 3
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 4,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2,
    // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 4,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2,
    // 5
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 2, 5,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3,
    // 6
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 0, 2, 6,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3,
    // 7
    0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 7,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 3,
    // 8
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 8,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3,
    // 9
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 2, 3, 8,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3,
    // 10
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 2, 3, 9,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 4,
    // 11
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 2, 0, 4, 10,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 3, 6,
    // 12
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 2, 3, 4, 11,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 3, 6,
    // 13
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 0, 2, 0, 0, 5, 11,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 3, 4, 6,
    // 14
    0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 2, 2, 3, 4, 6, 13,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 4, 8,
    // 15
    0, 0, 0, 1, 1, 1, 1, 1, 0, 2, 2, 0, 0, 0, 7, 13,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 2, 3, 4, 5, 8,
    // 16
    0, 0, 0, 1, 1, 1, 1, 0, 0, 2, 2, 3, 4, 5, 7, 13,
    0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 4, 5, 9,
    // 17
    0, 1, 1, 1, 1, 1, 0, 0, 2, 2, 3, 0, 0, 6, 8, 14,
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 3, 4, 5, 6, 10,
    // 18
    0, 1, 1, 1, 1, 0, 2, 2, 2, 0, 3, 4, 5, 6, 10, 15,
    0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 4, 5, 7, 13,
    // 19
    0, 1, 1, 1, 0, 0, 2, 2, 0, 0, 0, 5, 0, 0, 10, 15,
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 4, 5, 6, 8, 12,
    // 20
    0, 1, 1, 0, 0, 0, 2, 2, 0, 0, 5, 6, 6, 0, 10, 15,
    0, 0, 0, 1, 1, 1, 1, 1, 2, 2, 3, 4, 5, 6, 8, 13,
    // 21
    0, 1, 1, 0, 0, 0, 2, 0, 3, 4, 4, 5, 7, 8, 10, 15,
    0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 4, 5, 6, 8, 13,
    // 22
    1, 1, 1, 0, 0, 0, 2, 0, 3, 4, 5, 6, 7, 8, 10, 16,
    0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 4, 5, 6, 7, 9, 13,
    // 23
    1, 1, 0, 0, 0, 2, 2, 0, 3, 0, 0, 0, 0, 8, 11, 16,
    0, 0, 1, 1, 1, 1, 1, 2, 2, 4, 5, 6, 7, 7, 9, 14,
    // 24
    1, 1, 0, 0, 0, 2, 2, 3, 3, 5, 6, 7, 0, 8, 11, 16,
    0, 0, 1, 1, 1, 1, 1, 2, 2, 4, 5, 6, 7, 7, 9, 14,
    // 25
    1, 1, 0, 0, 0, 2, 2, 3, 0, 5, 6, 7, 0, 0, 11, 16,
    0, 0, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 8, 9, 14,
    // 26
    1, 1, 0, 0, 0, 2, 2, 0, 4, 0, 0, 0, 8, 0, 11, 16,
    0, 0, 1, 1, 1, 1, 1, 2, 3, 5, 6, 7, 7, 8, 9, 14,
    // 27
    1, 1, 0, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 11, 16,
    0, 0, 1, 1, 1, 1, 1, 3, 5, 6, 7, 8, 8, 9, 9, 14,
    // 28
    1, 1, 0, 0, 2, 2, 0, 0, 0, 7, 0, 0, 0, 0, 12, 16,
    0, 0, 1, 1, 1, 1, 2, 3, 5, 6, 7, 8, 8, 9, 10, 14,
    // 29
    1, 1, 0, 0, 2, 2, 0, 0, 0, 0, 0, 0, 0, 0, 12, 16,
    0, 0, 1, 1, 1, 1, 3, 5, 7, 8, 8, 9, 9, 10, 10, 14,
    // 30
    1, 1, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 16,
    0, 0, 1, 1, 1, 2, 4, 6, 7, 8, 9, 9, 10, 10, 11, 14,
    // 31
    1, 0, 2, 2, 3, 0, 0, 0, 0, 0, 0, 0, 0, 12, 14, 16,
    0, 1, 1, 1, 2, 4, 6, 7, 8, 9, 10, 10, 11, 11, 12, 15,
};

void DrawFlareLayer(Surface* surface, i16 x, i16 y, const u8* flareData, int colourIndex) {
    y -= (HIGHLIGHTS_SIZE / 2) - 1;
    x += 1;
    int i = 0;
    for (; i < (HIGHLIGHTS_SIZE / 2) - 1; i++) {
        u8 width = flareData[i];
        if (width == 0) {
            continue;
        }

        i16 x1 = x - width;
        width = (width << 1) - 1;
        DrawSpanClipped(surface, x1, y + i, x1 + width, colourIndex);
        DrawSpanClipped(surface, x1, y + (HIGHLIGHTS_SIZE - 2) - i, x1 + width, colourIndex);
    }

    u8 width = flareData[i];
    i16 x1 = x - width;
    width = (width << 1) - 1;

    DrawSpanClipped(surface, x1, y + i, x1 + width, colourIndex);
}

void Surface_DrawFlare(Surface* surface, i16 x, i16 y, int diameter, u16 colour1, u16 colour2) {
//#ifdef FINSPECTOR
//    static b32 init = 0;
//    if (init == 0) {
//        Build_Stars("data/stars-small.png", Draw_FlairGraphics16, 45, 8, 15);
//        Build_Stars("data/stars.png", Draw_FlairGraphics32, 45, 16, 31);
//        init = 1;
//    }
//#endif
    i16 offset = (diameter + HIGHLIGHTS_SMALL) * HIGHLIGHTS_SIZE;
    u8* flareData;
#if HIGHLIGHTS_SIZE == 16
    flairData = Draw_FlairGraphics16;
#else
    flareData = sDrawFlareGraphics32;
#endif
    DrawFlareLayer(surface, x, y, flareData + offset, colour1);
    DrawFlareLayer(surface, x, y, flareData + offset + (HIGHLIGHTS_SIZE / 2), colour2);
}

typedef struct sBezierSubDivision {
    i32 steps;
    i32 scaled;

    i32 x;
    i32 y;
    i32 dx;
    i32 dy;
    i32 d2x;
    i32 d2y;
    i32 d3x;
    i32 d3y;
} BezierSubDivision;

BezierSubDivision InitBezierSubdivision(Vec2i16* pts) {
    BezierSubDivision result;
    result.x = ((i32)pts[0].x) << 16;
    result.y = ((i32)pts[0].y) << 16;

    i32 x1 = pts[1].x * 3 - pts[0].x * 3;
    i32 y1 = pts[1].y * 3 - pts[0].y * 3;

    i32 x2 = (pts[2].x * 3) - pts[1].x * 6 + pts[0].x * 3;
    i32 y2 = (pts[2].y * 3) - pts[1].y * 6 + pts[0].y * 3;

    i32 x3 = pts[3].x - pts[0].x + (pts[1].x * 3) - (pts[2].x * 3);
    i32 y3 = pts[3].y - pts[0].y + (pts[1].y * 3) - (pts[2].y * 3);

    // Calc approx len to find number of steps to render
    i32 lenX = (i32)pts[0].x + (i32)pts[1].x + (i32)pts[2].x + (i32)pts[3].x;
    i32 lenY = (i32)pts[0].y + (i32)pts[1].y + (i32)pts[2].y + (i32)pts[3].y;

    lenX = lenX >> 2;
    lenY = lenY >> 2;

    lenX = abs(lenX - pts[0].x);
    lenY = abs(lenY - pts[0].y);

    i32 len = lenX + lenY;
    if (len < BEZIER_STEP_3_LEN) {
        result.steps = 3;
        result.scaled = 14;

        x1 <<= 14;
        y1 <<= 14;

        x2 <<= 12;
        y2 <<= 12;

        x3 <<= 10;
        y3 <<= 10;
    } else if (len < BEZIER_STEP_7_LEN) {
        result.steps = 7;
        result.scaled = 13;

        x1 <<= 13;
        y1 <<= 13;

        x2 <<= 10;
        y2 <<= 10;

        x3 <<= 7;
        y3 <<= 7;
    } else if (len < BEZIER_STEP_15_LEN) {
        result.steps = 15;
        result.scaled = 12;

        x1 <<= 12;
        y1 <<= 12;

        x2 <<= 8;
        y2 <<= 8;

        x3 <<= 4;
        y3 <<= 4;
    } else {
        result.steps = 31;
        result.scaled = 11;

        x1 <<= 11;
        y1 <<= 11;

        x2 <<= 6;
        y2 <<= 6;

        x3 <<= 1;
        y3 <<= 1;
    }

    result.dx = x1 + x2 + x3;
    result.dy = y1 + y2 + y3;

    result.d3x = x3 * 6;
    result.d3y = y3 * 6;

    result.d2x = result.d3x + x2 * 2;
    result.d2y = result.d3y + y2 * 2;

    return result;
}

MINTERNAL void SpanRenderer_InsertPoint(SpanLine* restrict spanLine, i16 x1) {
    u16 pos = spanLine->num;
    while (pos) {
        i16 cx1 = spanLine->span[pos - 1];
        if (x1 < cx1) {
            spanLine->span[pos] = cx1;
        } else {
            spanLine->span[pos] = x1;
            spanLine->num++;
            return;
        }
        pos--;
    }
    spanLine->span[pos] = x1;
    spanLine->num++;
}

MINTERNAL void SpanRenderer_AddLine(SpanRenderer* spans, i16 x1, i16 y1, i16 x2, i16 y2) {
    if (!ClipLineY(&x1, &y1, &x2, &y2, spans->height)) {
        return;
    }

    if (y1 > y2) {
        MSWAP(x1, x2, i16);
        MSWAP(y1, y2, i16);
    }

    if (y2 - 1 > spans->spanEnd) {
        if (y2 > spans->height) {
            y2 = spans->height;
        }

        spans->spanEnd = y2 - 1;
    }

    if (y1 < spans->spanStart) {
        spans->spanStart = y1;
    }

    int dx = x2 - x1;
    int dy = y2 - y1;

    if (dy == 0) {
        return;
    }

    int ddx = (dx << 16) / dy;
    int x32 = x1 << 16;

    SpanLine* spanLine = spans->spans + y1;
    i16 yRemain = y2 - y1;
    for (; yRemain > 0; yRemain--) {
        i16 x = x32 >> 16;
        SpanRenderer_InsertPoint(spanLine, x);
        x32 += ddx;
        spanLine++;
    }
}

MINTERNAL void SpanRenderer_AddLinesForBezier(SpanRenderer *spans, Vec2i16* pts) {
    BezierSubDivision subDivision = InitBezierSubdivision(pts);

    for (i32 i = 0; i <= subDivision.steps; i++) {
        i32 x1 = subDivision.x >> 16;
        i32 y1 = subDivision.y >> 16;

        subDivision.x += subDivision.dx;
        subDivision.dx += subDivision.d2x;
        subDivision.d2x += subDivision.d3x;

        subDivision.y += subDivision.dy;
        subDivision.dy += subDivision.d2y;
        subDivision.d2y += subDivision.d3y;

        i32 x2 = subDivision.x >> 16;
        i32 y2 = subDivision.y >> 16;

        SpanRenderer_AddLine(spans, x1, y1, x2, y2);
    }
}

MINTERNAL void SpanRenderer_Clear(SpanRenderer *spanRenderer) {
    spanRenderer->spans = NULL;
    spanRenderer->memSize = 0;
    spanRenderer->height = 0;
    spanRenderer->spanStart = 0;
    spanRenderer->spanEnd = 0;
}

MINTERNAL void SpanRenderer_Free(SpanRenderer *spanRenderer) {
    MFree(spanRenderer->spans); spanRenderer->spans = NULL;
}

MINTERNAL void SpanRenderer_Init(SpanRenderer *spanRenderer, u16 height) {
    u32 size = (height * sizeof(SpanLine));
    if (spanRenderer->memSize < size) {
        SpanRenderer_Free(spanRenderer);
    }

    if (!spanRenderer->spans) {
        spanRenderer->spans = (SpanLine*)MMalloc(size);
        spanRenderer->memSize = size;
    }

    spanRenderer->height = height;
    spanRenderer->spanStart = height + 1;
    spanRenderer->spanEnd = 0;

    for (int i = 0; i < height; i++) {
        spanRenderer->spans[i].num = 0;
    }
}

MINTERNAL void SpanRenderer_Print(SpanRenderer* spans, Surface* surface) {
#ifdef FINSPECTOR
    MLogf("spans %x", surface->insOffsetTmp);
#else
    MLog("spans");
#endif
    for (i16 row = spans->spanStart; row <= spans->spanEnd; ++row) {
        SpanLine* spanLine = spans->spans + row;
        u16 nSpansRow = spanLine->num;
        if (nSpansRow == 0) {
            continue;
        }

        printf("%x : %x : ", (int)row, nSpansRow);
        for (u16 i = 0; i < nSpansRow; i += 1) {
            int x1 = spanLine->span[i];
            printf("%x ", x1);
        }
        printf("\n");
    }

    MLog("done");
}

MINTERNAL void SpanRenderer_Draw(SpanRenderer *spans, Surface *surface, u8 colour) {
    // SpanPrint(spans, surface);

    u8* pixelsLine = surface->pixels + (spans->spanStart * SURFACE_WIDTH);
    SpanLine* spanLine = spans->spans + spans->spanStart;
    i16 rowsLeft =  spans->spanEnd - spans->spanStart;
    for (; rowsLeft >= 0; rowsLeft--) {
        for (u16 i = 0; (i + 1) < spanLine->num; i += 2) {
            i16 x1 = spanLine->span[i];
            i16 x2 = spanLine->span[i+1];

            if (x1 < 0) {
                x1 = 0;
            }

            if (x2 > SURFACE_WIDTH) {
                x2 = SURFACE_WIDTH;
            }

#ifdef FINSPECTOR
//            for (; x1 < x2; ++x1) {
//                int d = (row * SURFACE_WIDTH);
//                surface->insOffset[d + x1] = surface->insOffsetTmp;
//            }
#endif
            DrawSpanNoClip(pixelsLine, x1, x2, colour);
        }
        pixelsLine += SURFACE_WIDTH;
        spanLine++;
    }
}

MINTERNAL void BodySpans_Clear(BodySpanRenderer* spanRenderer) {
    spanRenderer->spans = NULL;
    spanRenderer->rowBeginColour = NULL;
    spanRenderer->maxSpan = 0;
    spanRenderer->spanStart = 0;
    spanRenderer->spanEnd = 0;
    spanRenderer->height = 0;
}

MINTERNAL void BodySpans_Free(BodySpanRenderer* spanRenderer) {
    if (spanRenderer->spans) {
        MFree(spanRenderer->spans); spanRenderer->spans = NULL;
    }
    if (spanRenderer->rowBeginColour) {
        MFree(spanRenderer->rowBeginColour); spanRenderer->rowBeginColour = NULL;
    }
}

MINTERNAL void BodySpans_Init(BodySpanRenderer* spanRenderer, u16 height) {
    if (spanRenderer->height < height) {
        BodySpans_Free(spanRenderer);
        spanRenderer->spans = (BodySpan*)MMalloc(height * sizeof(BodySpan));
        spanRenderer->rowBeginColour = (u16*)MMalloc(height * sizeof(u16));
        spanRenderer->height = height;
    }

    spanRenderer->maxSpan = 0;
    spanRenderer->spanStart = 0;
    spanRenderer->spanEnd = 0;

    u16 maxSpansPerLine = 4;

    spanRenderer->maxSpan = height;
    spanRenderer->spanStart = height;
    spanRenderer->spanEnd = -1;
    spanRenderer->maxSpansPerLine = maxSpansPerLine;

    for (int i = 0; i < height; i++) {
        BodySpan* span = spanRenderer->spans + i;
        span->num = 0;
        spanRenderer->rowBeginColour[i] = 0;
    }
}

MINTERNAL void BodySpans_XorColour(BodySpanRenderer* spans, u16 offset, u16 colour) {
    spans->rowBeginColour[offset] = spans->rowBeginColour[offset] ^ colour;
}

MINTERNAL void BodySpans_InsertPoint(BodySpan* bodySpan, i16 x1, u16 colour) {
    i16 num = bodySpan->num;
    if (num < BODY_MAX_SPANS) {
        Span* span = bodySpan->s + num;
        while (num > 0) {
            Span* prev = span - 1;
            if (x1 >= prev->x) {
                break;
            }
            *span = *prev;
            span--;
            num--;
        }

        span->colour = colour;
        span->x = x1;
        bodySpan->num += 1;
    }
}

MINTERNAL void BodySpans_AddLine(BodySpanRenderer* spans, i16 x1, i16 y1, i16 x2, i16 y2, u16 colour) {
    if (!ClipLineY(&x1, &y1, &x2, &y2, spans->height)) {
        return;
    }

    if (y1 > y2) {
        MSWAP(x1, x2, i16);
        MSWAP(y1, y2, i16);
    }

    if (y2 >= spans->height) {
        y2 = spans->height;
    }

    if (y2 - 1 > spans->spanEnd) {
        spans->spanEnd = y2 - 1;
    }

    if (y1 < spans->spanStart) {
        spans->spanStart = y1;
    }

    int dx = x2 - x1;
    int dy = y2 - y1;

    if (dy == 0) {
        return;
    }

    int decInc = (dx << 16) / dy;
    int d1 = x1 << 16;

    BodySpan* spanLine = spans->spans + y1;
    for (; y1 < y2; y1 += 1) {
        i16 x = d1 >> 16;
        BodySpans_InsertPoint(spanLine, x, colour);
        d1 += decInc;
        spanLine++;
    }
}

MINTERNAL void BodySpans_AddBezier(BodySpanRenderer* spanRenderer, Vec2i16* pts, u16 colour) {
    BezierSubDivision subDivision = InitBezierSubdivision(pts);

    for (i32 i = 0; i <= subDivision.steps; i++) {
        i32 x1 = subDivision.x >> 16;
        i32 y1 = subDivision.y >> 16;

        subDivision.x += subDivision.dx;
        subDivision.dx += subDivision.d2x;
        subDivision.d2x += subDivision.d3x;

        subDivision.y += subDivision.dy;
        subDivision.dy += subDivision.d2y;
        subDivision.d2y += subDivision.d3y;

        i32 x2 = subDivision.x >> 16;
        i32 y2 = subDivision.y >> 16;

        BodySpans_AddLine(spanRenderer, x1, y1, x2, y2, colour);
    }
}

MINTERNAL void BodySpans_Print(BodySpanRenderer* spans) {
    int spansY = spans->spanEnd;
    if (spansY <= 0) {
        return;
    }

    spansY--;

    BodySpan* bodySpan = spans->spans + spansY;

    u16 rowBeginColour = spans->rowBeginColour[spans->height - 1];
    u16* rowBeginColours = spans->rowBeginColour + spansY + 1;
    if (spansY >= SURFACE_HEIGHT - 1) {
        rowBeginColour = 0;
    }

    MLogf("----- %x -----", rowBeginColour);

    do {
        rowBeginColours--;
        rowBeginColour = *rowBeginColours;

        if (bodySpan->num) {
            printf("%x : %x ", spansY, bodySpan->num);
            for (int i = 0; i < bodySpan->num; ++i) {
                printf(", %x %x", bodySpan->s[i].colour, bodySpan->s[i].x);
            }
            MLogf(" : %x", rowBeginColour);
        } else {
            MLogf("%x: 0", spansY);
        }

        bodySpan--;
        spansY--;
    } while (spansY >= 0);

    MLogf("-----");
}

MINTERNAL void BodySpans_Draw(BodySpanRenderer* spans, Surface* surface) {
//     BodySpans_Print(spans);
    int spansY = spans->spanEnd;
    if (spansY <= 0) {
        return;
    }

    BodySpan* bodySpan = spans->spans + spansY;

    u16* rowBeginColours = spans->rowBeginColour + spansY + 1;
    u16 cRowBeginColour = spans->rowBeginColour[spans->height - 1];

    if (spansY >= SURFACE_HEIGHT - 1) {
        cRowBeginColour = 0;
    }

    u8* pixelsLine = surface->pixels + (spansY * SURFACE_WIDTH);

    do {
        rowBeginColours--;
        u16 cColour = *rowBeginColours;
        u16 pColour = cRowBeginColour;
        cRowBeginColour = pColour ^ cColour;
        u16 nSpansRow = bodySpan->num;

        if (nSpansRow > 1) {
            i16 j = nSpansRow;
            Span* span;
            for (u16 i = 0; i < nSpansRow; i++) {
                cColour = pColour ^ cColour;
                span = bodySpan->s + i;
                pColour = span->colour;
                if (pColour == 0) {
                    break;
                }
                j--;
            }
            do {
                i16 x1 = span->x;
                if (x1 < 0) {
                    x1 = 0;
                }
                if (x1 < SURFACE_WIDTH) {
                    span++;
                    i16 x2 = span->x;
                    if (x2 >= 0) {
                        if (x2 > SURFACE_WIDTH) {
                            x2 = SURFACE_WIDTH;
                        }
                        u16 colour = spans->colours[cColour / 4];
#ifdef FINSPECTOR
                        int d = (spansY * SURFACE_WIDTH);
#endif
                        for (; x1 < x2; ++x1) {
                            pixelsLine[x1] = colour;
#ifdef FINSPECTOR
                            surface->insOffset[d + x1] = surface->insOffsetTmp;
#endif
                        }
                    }
                }
                u16 colour = span->colour;
                if (colour == 0) {
                    break;
                }
                cColour = colour ^ cColour;
                j--;
            } while (j > 0);
        }
        bodySpan--;
        spansY--;
        pixelsLine -= SURFACE_WIDTH;
    } while (spansY >= spans->spanStart);
}

void Surface_DrawLine(Surface* surface, int x1, int y1, int x2, int y2, u8 colour) {
    if (y1 > y2) {
        MSWAP(y1, y2, int);
        MSWAP(x1, x2, int);
    }

    int xLen = x2 - x1;
    int yLen = y2 - y1;

    if (yLen > abs(xLen)) {
        int fixedDelta = (yLen == 0) ? 0 : ((xLen << 16) / yLen);
        int x = 0x8000 + (x1 << 16);
        u8* pixels = surface->pixels + y1 * SURFACE_WIDTH;
        while (yLen >= 0) {
            pixels[(x >> 16)] = colour;
            x += fixedDelta;
            pixels += SURFACE_WIDTH;
            yLen--;
        }
    } else {
        int deltaY = SURFACE_WIDTH;
        if (xLen < 0) {
            deltaY = -deltaY;
            y1 = y2;
            x1 = x2;
            xLen = - xLen;
        }
        int fixedDelta = ((((xLen + 1) << 16)) / (yLen + 1));
        int xx1 = ((x1) << 16) + 0x8000;
        int xx2 = xx1;
        u8* pixels = surface->pixels + y1 * SURFACE_WIDTH;
        while (yLen >= 0) {
            xx2 += fixedDelta;
            DrawSpanNoClip(pixels, (xx1 >> 16), (xx2 >> 16), colour);
            pixels += deltaY;
            yLen--;
            xx1 = xx2;
        }
    }
}

MINTERNAL void Surface_DrawBezierLine(Surface* surface, Vec2i16* pts, u8 colour) {
    BezierSubDivision subDivision = InitBezierSubdivision(pts);

    for (i32 i = 0; i <= subDivision.steps; i++) {
        Vec2i16 start;
        start.x = subDivision.x >> 16;
        start.y = subDivision.y >> 16;

        subDivision.x += subDivision.dx;
        subDivision.dx += subDivision.d2x;
        subDivision.d2x += subDivision.d3x;

        subDivision.y += subDivision.dy;
        subDivision.dy += subDivision.d2y;
        subDivision.d2y += subDivision.d3y;

        Vec2i16 end;
        end.x = subDivision.x >> 16;
        end.y = subDivision.y >> 16;

        if (ClipLinePoints(&start, &end, (surface->width - 1), (surface->height - 1))) {
            Surface_DrawLine(surface, start.x, start.y, end.x, end.y, colour);
        }
    }
}

void Surface_DrawLineClipped(Surface* surface, int x1, int y1, int x2, int y2, u8 colour) {
    Vec2i16 p1;
    p1.x = x1;
    p1.y = y1;

    Vec2i16 p2;
    p2.x = x2;
    p2.y = y2;

    if (ClipLinePoints(&p1, &p2, (surface->width-1), (surface->height-1))) {
#ifdef FINSPECTOR
        if (p1.x < 0 || p1.y < 0 || p1.x >= surface->width || p1.y >= surface->height ||
            p2.x < 0 || p2.y < 0 || p2.x >= surface->width || p2.y >= surface->height) {
            MLogf("Error: Line out %d %d %d %d", p1.x, p1.y, p2.x, p2.y);
        }
#endif
        Surface_DrawLine(surface, p1.x, p1.y, p2.x, p2.y, colour);
    }
}

MINLINE u8 Palette_GetDynamicColourIndex(RasterContext* context, u8 paletteIndex) {
    return context->paletteContext.palette[paletteIndex].index;
}

static u8 Palette_GetIndexFor12bitColour(PaletteContext* context, u16 colour12bit) {
    colour12bit &= 0xfff;
    u8 index = context->allColours[colour12bit];
    if (index == 0xff) {
        u8 newColourIndex = context->nextFreeColour;
        newColourIndex = context->freeColours[newColourIndex];
        if (newColourIndex == 0xff) {
            // no more available just return colour zero
            return context->allColours[0];
        }

        context->nextFreeColour++;
        context->allColours[colour12bit] = newColourIndex;
        context->palette[newColourIndex].state = PALETTESTATE_REQUEST;
        context->palette[newColourIndex].colourOf4096 = colour12bit;
        return newColourIndex;
    } else {
        if (context->palette[index].state >= PALETTESTATE_REFD_NON_FRESH) {
            context->palette[index].state = PALETTESTATE_REFD_FRESH;
        }
        return index;
    }
}

MINLINE RGB Colour12ConvertToRGB(u16 colour12) {
    RGB rgb = {((colour12 & 0xf00) >> 4) + 0xf, ((colour12 & 0x0f0)) + 0xf, ((colour12 & 0x00f) << 4) + 0xf};
    return rgb;
}

MINTERNAL void SetDefaultBackgroundColour(PaletteContext* context) {
    context->backgroundColour = 0x005;
}

void Palette_SetupForNewFrame(PaletteContext* context, b32 resetAll) {
    if (resetAll) {
        memset(context->allColours, 0xff, 4096);

        for (u8 i = 0; i < 16; ++i) {
            context->palette[i].state = PALETTESTATE_FIXED;
            context->palette[i].index = i;

            u16 gray = i;
            u16 colour = gray << 8 | gray << 4 | gray;
            context->palette[i].colourOf4096 = colour;
            context->allColours[colour] = i;
        }

        context->lastBackgroundColour = 0;

        for (int i = 16; i < 0x100; ++i) {
            context->palette[i].colourOf4096 = 0x0;
            context->palette[i].index = 0x0;
            context->palette[i].state = PALETTESTATE_FREE;
        }
    }

    SetDefaultBackgroundColour(context);

    int nextNewColour = 0;
    for (int i = 0; i < 0x100; ++i) {
        u8 state = context->palette[i].state;
        switch (state) {
            case PALETTESTATE_FIXED:
            case PALETTESTATE_REQUEST:
            case PALETTESTATE_REFD_NON_FRESH:
                break;
            case PALETTESTATE_MATCHED: {
                u16 colour = context->palette[i].colourOf4096;
                context->allColours[colour] = 0xff;
                context->palette[i].state = PALETTESTATE_FREE;
            }
            case PALETTESTATE_FREE:
                context->freeColours[nextNewColour++] = i;
                break;
            default:
                context->palette[i].state--;
                break;
        }
    }

    context->freeColours[nextNewColour] = 0xff;
    context->nextFreeColour = 0;
}

typedef enum ePaletteEntryUnusedStateEnum {
    PALETTEENTRY_FREE = 0,  // Never allocated / previously free'd
    PALETTEENTRY_USED = 1,  // Currently allocated, but not used this frame
    PALETTEENTRY_UNUSED = 2 // Currently allocated and used this frame
} PaletteEntryUnusedStateEnum;

typedef struct sPaletteEntryUnused {
    PaletteEntryUnusedStateEnum state;
    u8 index;
} PaletteEntryUnused;

void Palette_CalcDynamicColourUpdates(PaletteContext* context) {
    PaletteEntryUnused paletteEntryState[0x80];
    u16 freeColoursIx[0x80];
    u16 freeColours[0x80];

    UpdateColour* updateColours = context->updateColours;

    memset(paletteEntryState, 0x00, sizeof(paletteEntryState));

    for (int i = 0; i < 0xff; ++i) {
        u8 pState = context->palette[i].state;
        if (pState >= PALETTESTATE_REFD_NON_FRESH) {
            u8 a = context->palette[i].index;
            if (a >= 0x80) {
                PaletteEntryUnusedStateEnum state;
                if (pState == PALETTESTATE_REFD_NON_FRESH) {
                    state = PALETTEENTRY_UNUSED;
                } else {
                    state = PALETTEENTRY_USED;
                }
                paletteEntryState[a - 0x80].state = state;
                paletteEntryState[a - 0x80].index = i;
            }
        }
    }

    // First add any palette entry's not allocated to free list
    int o = 0;
    for (int i = 0; i < 0x80; ++i) {
        PaletteEntryUnusedStateEnum state = paletteEntryState[i].state;
        if (state == PALETTEENTRY_FREE) {
            freeColoursIx[o] = i + 0x80;
            freeColours[o] = 0xff;
            o++;
        }
    }

    // Then add any colours not used this frame to free list
    for (int i = 0; i < 0x80; ++i) {
        PaletteEntryUnusedStateEnum state = paletteEntryState[i].state;
        if (state == PALETTEENTRY_UNUSED) {
            freeColoursIx[o] = i + 0x80;
            freeColours[o] = paletteEntryState[i].index;
            o++;
        }
    }

    freeColoursIx[o] = 0;
    u16 nextFreeColour = 0;

    int updateColourIndex = 0;

    for (int i = 0; i < 0xff; ++i) {
        u8 pState = context->palette[i].state;
        if (pState == PALETTESTATE_REQUEST) {
            u16 colour = context->palette[i].colourOf4096;
            u8 matchedOffset = 0;

            u8 pOutOffset = freeColoursIx[nextFreeColour];
            if (pOutOffset != 0) {
                matchedOffset = freeColours[nextFreeColour++];
                if (matchedOffset != 0xff) {
                    context->palette[matchedOffset].state = PALETTESTATE_FREE;
                    u16 colourToFree = context->palette[matchedOffset].colourOf4096;
                    context->allColours[colourToFree] = 0xff;
                }

                context->palette[i].state = PALETTESTATE_REFD_FRESH;
                context->palette[i].index = pOutOffset;

                updateColours[updateColourIndex].index = pOutOffset;
                updateColours[updateColourIndex].colour = colour;

                updateColourIndex++;
            } else {
                // No more free / used colours find existing colour that matches closest
                u16 newColour = colour;
                while (newColour != 0) {
                    if ((newColour & 0xf00) != 0) {
                        newColour -= 0x100;
                        matchedOffset = context->allColours[newColour];
                        if (matchedOffset != 0xff && context->palette[matchedOffset].state != PALETTESTATE_REQUEST) {
                            goto found;
                        }
                    }
                    if ((newColour & 0x0f0) != 0) {
                        newColour -= 0x10;
                        matchedOffset = context->allColours[newColour];
                        if (matchedOffset != 0xff && context->palette[matchedOffset].state != PALETTESTATE_REQUEST) {
                            goto found;
                        }
                    }
                    if ((newColour & 0x00f) != 0) {
                        newColour -= 0x1;
                        matchedOffset = context->allColours[newColour];
                        if (matchedOffset != 0xff && context->palette[matchedOffset].state != PALETTESTATE_REQUEST) {
                            goto found;
                        }
                    }
                }
                found:

                pState = context->palette[matchedOffset].state;
                if (pState >= PALETTESTATE_REFD_NON_FRESH) {
                    context->palette[matchedOffset].state = PALETTESTATE_REFD_FRESH;
                }
                context->palette[i].index = context->palette[matchedOffset].index;
                context->palette[i].state = PALETTESTATE_MATCHED;
            }
        }
    }

    if (context->lastBackgroundColour != context->backgroundColour) {
        u16 colour = context->backgroundColour;

        updateColours[updateColourIndex].index = BACKGROUND_COLOUR_INDEX;
        updateColours[updateColourIndex].colour = colour;
        updateColourIndex++;

        context->lastBackgroundColour = colour;
    }

    context->updatedColoursNum = updateColourIndex;
}

void Palette_CopyDynamicColoursRGB(PaletteContext* context, RGB* palette) {
    UpdateColour* updateColours = context->updateColours;
    u32 numUpdateColours = context->updatedColoursNum;

    for (int i = 0; i < numUpdateColours; ++i) {
        UpdateColour* colour = updateColours + i;
        RGB* rgb = &(palette[colour->index]);
        *rgb = Colour12ConvertToRGB(colour->colour);
    }
}

void Palette_CopyFixedColoursRGB(PaletteContext* context, RGB* palette) {
    for (int i = 0; i < 16; ++i) {
        DynamicPalette colour = context->palette[i];

        RGB* rgb = &(palette[colour.index]);

        u16 colour12 = colour.colourOf4096;
        *rgb = Colour12ConvertToRGB(colour12);
    }
}

void Palette_CopyAllColoursRGB(PaletteContext* context, RGB* palette) {
    palette[BACKGROUND_COLOUR_INDEX] = Colour12ConvertToRGB(context->backgroundColour);

    for (int i = 0; i < 0xff; ++i) {
        DynamicPalette colour = context->palette[i];

        RGB* rgb = &(palette[colour.index]);

        u16 colour12 = 0;
        if (i > 15) {
            colour12 = colour.colourOf4096;
        } else {
            u16 gray = 15 - i;
            colour12 = (gray << 8u) + (gray << 4u) + gray;
        }

        *rgb = Colour12ConvertToRGB(colour12);
    }
}

void Palette_CopyDynamicColours16(PaletteContext* context, u16* palette) {
    for (int i = 0; i < 0xff; ++i) {
        DynamicPalette colour = context->palette[i];

        if (colour.state > 0 &&
            colour.state != PALETTESTATE_FREE &&
            colour.state != PALETTESTATE_REFD_NON_FRESH) {

            palette[colour.index] = colour.colourOf4096;
        }
    }
}

void Palette_CopyFixedColours16(PaletteContext* context, u16* palette) {
    palette[BACKGROUND_COLOUR_INDEX] = context->backgroundColour;

    for (int i = 0; i < 16; ++i) {
        DynamicPalette colour = context->palette[i];
        palette[colour.index] = colour.colourOf4096;
    }
}

MINTERNAL void ZTree_Clear(ZTree* zTree) {
#ifdef FINSPECTOR
    memset(zTree->insOffset, 0, zTree->size * sizeof(u32));
    zTree->insOffsetTmp = 0;
#endif

    RasterOpNode* root = (RasterOpNode*)zTree->data;
    root->z = 0;
    root->leftOffset = 0;
    root->rightOffset = 0;
    root->func.func = DRAW_FUNC_NULL;
    zTree->firstNodeOffset = 0;
    zTree->offset = sizeof(RasterOpNode);
}

MINTERNAL void ZTree_Init(ZTree* zTree, u32 size) {
    zTree->size = size;
    u16 maxDrawNodeSize = 0x200;
    zTree->data = (u8*)MMalloc(size + maxDrawNodeSize);
    zTree->root = zTree->data;

    MArrayInit(zTree->subTrees);

#ifdef FINSPECTOR
    zTree->insOffset = (u32*)MMalloc((size + maxDrawNodeSize) * sizeof(u32));
    zTree->insOffsetTmp = 0;
#endif

    ZTree_Clear(zTree);
}

MINTERNAL void ZTree_Free(ZTree* zTree) {
    MFree(zTree->data); zTree->data = 0;
    MArrayFree(zTree->subTrees);
}

MINTERNAL RasterOpNode* ZTree_AddNode(ZTree* zTree, u32 z) {
    RasterOpNode* node = (RasterOpNode*)zTree->root;
    u32 offset = zTree->offset;

    while (node) {
        if (z <= node->z) {
            if (!node->leftOffset) {
                node->leftOffset = offset;
                break;
            } else {
                node = (RasterOpNode *) (zTree->data + node->leftOffset);
            }
        } else {
            if (!node->rightOffset) {
                node->rightOffset = offset;
                break;
            } else {
                node = (RasterOpNode *) (zTree->data + node->rightOffset);
            }
        }
    }

    RasterOpNode* newNode = (RasterOpNode *) (zTree->data + offset);
    newNode->z = z;
    newNode->leftOffset = 0;
    newNode->rightOffset = 0;
    zTree->offset += sizeof(RasterOpNode);

#ifdef FINSPECTOR
    *(zTree->insOffset + offset) = zTree->insOffsetTmp;
#endif

    return newNode;
}

MINTERNAL void ZTree_PushSubTree(ZTree* zTree, i32 z) {
    RasterOpNode* drawNode = ZTree_AddNode(zTree, z);
    drawNode->func.func = DRAW_FUNC_SUBTREE;

    MArrayAdd(zTree->subTrees, zTree->root);

    RasterOpNode* rootNode = drawNode + 1;
    rootNode->z = 0;
    rootNode->leftOffset = 0;
    rootNode->rightOffset = 0;
    rootNode->func.func = DRAW_FUNC_NULL;
    zTree->root = (u8*)(rootNode);
}

MINTERNAL void ZTree_PopSubTree(ZTree* zTree) {
    zTree->root = MArrayPop(zTree->subTrees);
}

void Raster_Init(RasterContext* raster) {
    SpanRenderer_Clear(&(raster->spanRenderer));
    BodySpans_Clear(&(raster->bodySpanRenderer));

    u16 maxDrawBufSize = 0xffff;
    ZTree_Init(&(raster->zTree), maxDrawBufSize);

    MArrayInit(raster->drawNodeStack);
}

void Raster_Free(RasterContext* raster) {
    ZTree_Free(&raster->zTree);
    BodySpans_Free(&(raster->bodySpanRenderer));
    SpanRenderer_Free(&(raster->spanRenderer));
    MArrayFree(raster->drawNodeStack);
}

MINTERNAL void DoRasterTree(RasterContext* raster, u8* mem, RasterOpNode* drawNode);

MINTERNAL int DoDrawFunc(RasterContext *context, DrawFunc *drawFunc) {
    switch (drawFunc->func) {
        // Spans
        case DRAW_FUNC_SPANS_START:
            SpanRenderer_Init(&context->spanRenderer, context->surface->height);
            return 0;
        case DRAW_FUNC_SPANS_BEZIER: {
            DrawParamsBezier *params = (DrawParamsBezier *) (&drawFunc->params);
            SpanRenderer_AddLinesForBezier(&(context->spanRenderer), params->pts);
            return sizeof(DrawParamsBezier);
        }
        case DRAW_FUNC_SPANS_LINE: {
            DrawParamsLine *params = (DrawParamsLine *) (&drawFunc->params);
            SpanRenderer_AddLine(&(context->spanRenderer), params->x1, params->y1, params->x2, params->y2);

            return sizeof(DrawParamsLine);
        }
        case DRAW_FUNC_SPANS_LINE_CONT: {
            DrawParamsPoint *param1 = (DrawParamsPoint *) (((u8 *) &drawFunc->func) - sizeof(DrawParamsPoint));
            DrawParamsPoint *param2 = (DrawParamsPoint *) (&drawFunc->params);

            SpanRenderer_AddLine(&(context->spanRenderer), param1->x, param1->y, param2->x, param2->y);
            return sizeof(DrawParamsPoint);
        }
        case DRAW_FUNC_SPANS_POINT: {
            return sizeof(DrawParamsPoint);
        }
        case DRAW_FUNC_SPANS_DRAW: {
            DrawParamsColour *params = (DrawParamsColour *) (&drawFunc->params);

//            SpanRenderer_Print(&context->spanRenderer, context->surface);

            SpanRenderer_Draw(&context->spanRenderer, context->surface,
                              Palette_GetDynamicColourIndex(context, params->colour));

            SpanRenderer_Init(&context->spanRenderer, context->surface->height);

            return sizeof(DrawParamsColour);
        }
        case DRAW_FUNC_SPANS_END: {
            DrawParamsColour *params = (DrawParamsColour *) (&drawFunc->params);

//            SpanRenderer_Print(&context->spanRenderer, context->surface);

            SpanRenderer_Draw(&context->spanRenderer, context->surface,
                              Palette_GetDynamicColourIndex(context, params->colour));

            return sizeof(DrawParamsColour);
        }
        case DRAW_FUNC_BATCH_END1: {
            return -1;
        }
        case DRAW_FUNC_BATCH_END: {
            return -2;
        }
        case DRAW_FUNC_NULL: {
            return -3;
        }
        case DRAW_FUNC_NOP: {
            return 0;
        }
        case DRAW_FUNC_LINE: {
            DrawParamsLineColour* params = (DrawParamsLineColour*)(&drawFunc->params);

            Surface_DrawLineClipped(context->surface, params->x1, params->y1, params->x2, params->y2,
                                    Palette_GetDynamicColourIndex(context, params->colour));

            return sizeof(DrawParamsLineColour);
        }
        case DRAW_FUNC_TRI: {
            DrawParamsTri* params = (DrawParamsTri*)(&drawFunc->params);

            Surface_DrawTriFill(context->surface, params->points, Palette_GetDynamicColourIndex(context, params->colour));

            return sizeof(DrawParamsTri);
        }
        case DRAW_FUNC_QUAD: {
            DrawParamsQuad* params = (DrawParamsQuad*)(&drawFunc->params);

            Surface_DrawQuadFill(context->surface, params->points, Palette_GetDynamicColourIndex(context, params->colour));

            return sizeof(DrawParamsQuad);
        }
        case DRAW_FUNC_BEZIER_LINE: {
            DrawParamsBezierColour* params = (DrawParamsBezierColour*)(&drawFunc->params);
            Surface_DrawBezierLine(context->surface, params->pts,
                                   Palette_GetDynamicColourIndex(context, params->colour));
            return sizeof(DrawParamsBezierColour);
        }
        case DRAW_FUNC_FLARE: {
            DrawParamsFlare* params = (DrawParamsFlare*)(&drawFunc->params);

            Surface_DrawFlare(context->surface, params->x, params->y, params->diameter,
                              Palette_GetDynamicColourIndex(context, params->outerColour),
                              Palette_GetDynamicColourIndex(context, params->innerColour));

            return sizeof(DrawParamsFlare);
        }
        case DRAW_FUNC_CIRCLE: {
            DrawParamsCircle* params = (DrawParamsCircle*)(&drawFunc->params);

            Surface_DrawCircleFill(context->surface, params->x, params->y, params->diameter,
                                   Palette_GetDynamicColourIndex(context, params->colour));

            return sizeof(DrawParamsCircle);
        }
        case DRAW_FUNC_RINGED_CIRCLE: {
            DrawParamsRingedCircle* params = (DrawParamsRingedCircle*)(&drawFunc->params);

            u16 d1;
            u16 d2;

            if (params->diameter < 11) {
                d1 = sRingedCircleDiametersInner[params->diameter];
                d2 = sRingedCircleDiametersOuter[params->diameter];
            } else {
                d1 = params->diameter;
                d2 = params->diameter + (params->diameter >> 3);
            }

            Surface_DrawCircleFill(context->surface, params->x, params->y, d2,
                                   Palette_GetDynamicColourIndex(context, params->outerColour));

            Surface_DrawCircleFill(context->surface, params->x, params->y, d1,
                                   Palette_GetDynamicColourIndex(context, params->innerColour));

            return sizeof(DrawParamsRingedCircle);
        }
        case DRAW_FUNC_TEXT: {
            DrawParamsText* params = (DrawParamsText*)(&drawFunc->params);
            return sizeof(DrawParamsText);
        }
        case DRAW_FUNC_BALLS: {
            if (context->legacy) {
                DrawParamsLegacyBalls* params = (DrawParamsLegacyBalls*) (&drawFunc->params);

                u16 colour = Palette_GetDynamicColourIndex(context, params->colour);

                u16 d = params->width;

                int i = 0;
                while (params->pos[i] != 0xffff) {
                    i16 x = params->pos[i++];
                    i16 y = params->pos[i++];
                    Surface_DrawCircleFill(context->surface, x, y, d, colour);
                }

                return sizeof(DrawParamsLegacyBalls) + ((i + 1) * 2);
            } else {
                DrawParamsBalls* params = (DrawParamsBalls*) (&drawFunc->params);

                u16 colour = Palette_GetDynamicColourIndex(context, params->colour);

                u16 d = params->width;

                for (int i = 0; i < params->num;) {
                    i16 x = params->pos[i++];
                    i16 y = params->pos[i++];
                    Surface_DrawCircleFill(context->surface, x, y, d, colour);
                }

                return sizeof(DrawParamsBalls) + (params->num * 2);
            }
        }
        case DRAW_FUNC_BODY_START: {
            BodySpans_Init(&context->bodySpanRenderer, context->surface->height);
            return 0;
        }
        case DRAW_FUNC_BODY_BEZIER: {
            DrawParamsBezierColour *params = (DrawParamsBezierColour *) (&drawFunc->params);
            BodySpans_AddBezier(&(context->bodySpanRenderer), params->pts, params->colour);
            return sizeof(DrawParamsBezierColour);
        }
        case DRAW_FUNC_BODY_LINE: {
            DrawParamsLineColour *params = (DrawParamsLineColour *) (&drawFunc->params);
            BodySpans_AddLine(&(context->bodySpanRenderer), params->x1, params->y1, params->x2, params->y2,
                              params->colour);
            return sizeof(DrawParamsLineColour);
        }
        case DRAW_FUNC_BODY_XOR: {
            DrawParamsBodyXor* params = (DrawParamsBodyXor*)(&drawFunc->params);
            BodySpans_XorColour(&(context->bodySpanRenderer), params->offset, params->colour);
            return sizeof(DrawParamsBodyXor);
        }
        case DRAW_FUNC_BODY_DRAW_1: {
            DrawParamsColour8* params = (DrawParamsColour8*)(&drawFunc->params);

            context->bodySpanRenderer.numColours = 8;
            for (int i = 0; i < context->bodySpanRenderer.numColours; ++i) {
                context->bodySpanRenderer.colours[i] = Palette_GetDynamicColourIndex(context, params->colour[i]);
            }

            BodySpans_Draw(&context->bodySpanRenderer, context->surface);
            return sizeof(DrawParamsColour8);
        }
        case DRAW_FUNC_BODY_DRAW_2: {
            DrawParamsColour16* params = (DrawParamsColour16*)(&drawFunc->params);

            context->bodySpanRenderer.numColours = 16;
            for (int i = 0; i < context->bodySpanRenderer.numColours; ++i) {
                context->bodySpanRenderer.colours[i] = Palette_GetDynamicColourIndex(context, params->colour[i]);
            }

            BodySpans_Draw(&context->bodySpanRenderer, context->surface);

            return sizeof(DrawParamsColour16);
        }
        case DRAW_FUNC_BODY_DRAW_3: {
            DrawParamsColour8* params = (DrawParamsColour8*)(&drawFunc->params);

            context->bodySpanRenderer.numColours = 8;
            for (int i = 0; i < context->bodySpanRenderer.numColours; ++i) {
                context->bodySpanRenderer.colours[i] = Palette_GetDynamicColourIndex(context, params->colour[i]);
            }

            BodySpans_Draw(&context->bodySpanRenderer, context->surface);

            return sizeof(DrawParamsColour8);
        }
        case DRAW_FUNC_SUBTREE: {
            RasterOpNode* subTree = (RasterOpNode*)(&drawFunc->params);
            DoRasterTree(context, context->zTree.data, subTree);
            return 0;
        }
        default:
            MLogf("ERROR Unknown draw type %4hx", drawFunc->func);
            return -1;
    }
}

MINTERNAL int DrawBatch(RasterContext* context, RasterOpNode* drawNode) {
    u8* currentFunc = (u8*)&drawNode->func;

    DrawFunc* drawFunc = (DrawFunc*)(currentFunc);

    // Draw batch
    int size = 0;
    do {
        currentFunc += sizeof(drawFunc->func);
        currentFunc += size;
        drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINSPECTOR
        u16 offset = (u8*)drawFunc - (u8*)context->zTree.data;
        context->surface->insOffsetTmp = context->zTree.insOffset[offset];
#endif
        size = DoDrawFunc(context, drawFunc);
    } while (size >= 0);

    return 0;
}

MINTERNAL int DoSpanFunc(RasterContext* context, RasterOpNode* drawNode) {
    u8* currentFunc = (u8*)&drawNode->func;

    DrawFunc* drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINSPECTOR
    u16 offset1 = (u8*)drawFunc - (u8*)context->zTree.data;
    context->surface->insOffsetTmp = context->zTree.insOffset[offset1];
#endif

    int size = 0;
    while (drawFunc->func != DRAW_FUNC_SPANS_END) {
        size = DoDrawFunc(context, drawFunc);
        currentFunc += sizeof(drawFunc->func);
        currentFunc += size;
        drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINSPECTOR
        u16 offset2 = (u8*)drawFunc - (u8*)context->zTree.data;
        context->surface->insOffsetTmp = context->zTree.insOffset[offset2];
#endif
    }

    DoDrawFunc(context, drawFunc);

    return size;
}

MINTERNAL int DoBodySpanFunc(RasterContext* context, RasterOpNode* drawNode) {
    u8* currentFunc = (u8*)&drawNode->func;

    DrawFunc* drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINSPECTOR
    u16 offset1 = (u8*)drawFunc - (u8*)context->zTree.data;
    context->surface->insOffsetTmp = context->zTree.insOffset[offset1];
#endif

    int size = 0;
    while (drawFunc->func != DRAW_FUNC_BODY_DRAW_1 &&
           drawFunc->func != DRAW_FUNC_BODY_DRAW_2 &&
           drawFunc->func != DRAW_FUNC_BODY_DRAW_3) {

        size = DoDrawFunc(context, drawFunc);
        currentFunc += sizeof(drawFunc->func);
        currentFunc += size;
        drawFunc = (DrawFunc*)(currentFunc);
#ifdef FINSPECTOR
        u16 offset2 = (u8*)drawFunc - (u8*)context->zTree.data;
        context->surface->insOffsetTmp = context->zTree.insOffset[offset2];
#endif
    }

    DoDrawFunc(context, drawFunc);

    return size;
}

MINTERNAL int DoRenderNode(RasterContext* context, RasterOpNode* renderNode) {
#ifdef FINSPECTOR
    u16 offset = (u8*)renderNode - (u8*)context->zTree.data;
    context->surface->insOffsetTmp = context->zTree.insOffset[offset];
#endif

    DrawFunc* drawFunc = &renderNode->func;

    switch (drawFunc->func) {
        case DRAW_FUNC_BATCH_START:
            DrawBatch(context, renderNode);
            break;
        case DRAW_FUNC_SPANS_START:
            DoSpanFunc(context, renderNode);
            break;
        case DRAW_FUNC_BODY_START:
            DoBodySpanFunc(context, renderNode);
            break;
        case DRAW_FUNC_NULL:
            break;
        case DRAW_FUNC_LINE:
        case DRAW_FUNC_TRI:
        case DRAW_FUNC_QUAD:
        case DRAW_FUNC_BEZIER_LINE:
        case DRAW_FUNC_FLARE:
        case DRAW_FUNC_CIRCLE:
        case DRAW_FUNC_RINGED_CIRCLE:
        case DRAW_FUNC_BALLS:
        case DRAW_FUNC_TEXT:
        case DRAW_FUNC_SUBTREE:
            DoDrawFunc(context, drawFunc);
            break;

        default:
            MLogf("ERROR Unknown draw type %4hx", drawFunc->func);
            return -1;
    }

    return 0;
}

MINTERNAL void DoRasterTree(RasterContext* raster, u8* mem, RasterOpNode* drawNode) {
    u32 initialSize = MArraySize(raster->drawNodeStack);

    do {
        while (drawNode != NULL) {
            MArrayAdd(raster->drawNodeStack, drawNode);
            if (drawNode->rightOffset) {
                drawNode = (RasterOpNode*)(mem + drawNode->rightOffset);
            } else {
                drawNode = NULL;
            }
        }

        drawNode = MArrayPop(raster->drawNodeStack);

        if (DoRenderNode(raster, drawNode) < 0) {
            break;
        }

        if (drawNode->leftOffset) {
            drawNode = (RasterOpNode*)(mem + drawNode->leftOffset);
        } else {
            drawNode = NULL;
        }
    } while (drawNode != NULL || MArraySize(raster->drawNodeStack) > initialSize);
}

void Raster_Draw(RasterContext *raster) {
    raster->paletteContext.nextFreeColour = 0;

    u8* zTreeMem = raster->zTree.data;

    RasterOpNode* drawNode = (RasterOpNode*)(zTreeMem + raster->zTree.firstNodeOffset);

    MArrayClear(raster->drawNodeStack);
    DoRasterTree(raster, zTreeMem, drawNode);
#ifdef MEMDEBUG
    // Check rasterer hasn't overwriten memory as far as we can tell
    MMemDebugCheck(raster->surface->pixels);
#ifdef FINSPECTOR
    MMemDebugCheck(raster->surface->insOffset);
#endif
#endif
}

MINTERNAL void CopyVertexView(VertexData* vertex, Vec3i32 d) {
    d[0] = vertex->vVec[0];
    d[1] = vertex->vVec[1];
    d[2] = vertex->vVec[2];
}

typedef struct sRenderFrame {
    // model normals offset (in modelData)
    // / char offset when rendering text
    u16 normalOffset;

    u16* vertexData; // into modelData->data, typically packed vertex data, vertex per dword

    SceneSetup* renderContext;

    // Current byte code position
    u8* byteCodePos;

    u16 scale; // model scaling factor

    VertexData* parentVertices;

    // Light direction
    Vec3i16 lightV;

    i16 zScale; // viewport vec transform scale

    u8 complexSkipIfPointZClipped;
    u16 complexNormalIndex;
    Vec3i32 complexNormal;
    i16 complexLastVertexIx;
    i16 complexBeginVertexIx;
    u16 complexCurrentlyZClipped;
    Vec2i16 complexLastZClipPt;
    u16 complexJoinToBeginning;
    u16 complexColour;

    i16 newMatrixWinding;
    i16 matrixWinding;

    // Base colour for current model
    u16 baseColour;

    u8 frameId; // each new transform increments this, so that cached vertices can be reprojected

    Vec3i32 lineStartVec;

    Matrix3x3i16 modelMatrix;

    u16 shadeRamp[8];

    VertexData* parentVertexTrans;
    i16 parentFrameVertexIndexes[5];

    // Tmp variables for byte code to write/read
    u16 tmpVariable[8];

    // View direction in model space
    Vec3i16 viewM;

    // Light direction in model space
    Vec3i16 lightM;

    // Matrix 3x3
    // Rotation from model space to view space
    // Contains both model and view rotations
    Matrix3x3i16 modelViewMatrix;

    u16 x12; // object vec scale?

    // Object to camera origin
    Vec3i32 objViewVec;

    // Object to camera origin scaling
    u16 objViewVecScale;

    // Model currently being rendered
    ModelData* modelData;

    u16 numVertexTmp;
    u16 numVertexModel;
    VertexData* vertexTrans;

    u16 numNormals;
    u16* normals;

#ifdef FINSPECTOR
    u8* fileDataStartAddress;
#endif
} RenderFrame;

MINTERNAL b32 ByteCodeIsDone(RenderFrame* rf) {
    return rf->byteCodePos == NULL;
}

MINTERNAL void ByteCodeSkipToEnd(RenderFrame* rf) {
    rf->byteCodePos = NULL;
}

MINTERNAL void ByteCodeSkipBytes(RenderFrame* rf, int bytes) {
    rf->byteCodePos += bytes;
}

MINTERNAL i8 ByteCodeRead8i(RenderFrame* rf) {
    i8 v = *((i8*)rf->byteCodePos);
    rf->byteCodePos += 1;
    return v;
}

MINTERNAL u8 ByteCodeRead8u(RenderFrame* rf) {
    u8 v = *((u8*)rf->byteCodePos);
    rf->byteCodePos += 1;
    return v;
}

MINTERNAL u16 ByteCodeRead16u(RenderFrame* rf) {
    u16 v = *((u16*)rf->byteCodePos);
    rf->byteCodePos += 2;
    return v;
}

MINTERNAL i16 ByteCodeRead16i(RenderFrame* rf) {
    u16 v = *((i16*)rf->byteCodePos);
    rf->byteCodePos += 2;
    return v;
}

MINTERNAL u16 ByteCodePeek16u(RenderFrame* rf) {
    return *((i16*)rf->byteCodePos);
}

MINTERNAL void GetNormalVec3i32(RenderFrame* rf, u8 normalIndex, Vec3i32 n) {
    u16 nOffset = rf->normalOffset;

    nOffset = nOffset + ((normalIndex & 0x7e) << 1);

    i16* normalDataPtr = (i16*)(((u8*)rf->modelData) + nOffset);

    u16 normalPacked1 = *(normalDataPtr - 2);
    u16 normalPacked2 = *(normalDataPtr - 1);

    n[0] = lo8s(normalPacked1) << 8;
    n[1] = (i16)(normalPacked2 & 0xff00);
    n[2] = lo8s(normalPacked2) << 8;

    if (normalIndex & 0x1) {
        n[0] = -n[0];
    }
}

#define RENDER_FRAMES_DEPTH 0x20

typedef struct sRenderContext {
    RenderFrame renderFrame[RENDER_FRAMES_DEPTH];
    i32 currentRenderFrameIx;
    RenderFrame* currentRenderFrame;

    u16 currentBatchId;

    u32 random1;
    u32 random2;

    ZTree* zTree;
    DrawFunc* lastBatchFunc;
    u32 lastBatchOffset;

    u16 width;
    u16 height;

    u16 renderDetail1;
    u16 renderDetail2;

    i16 renderPlanetAtmos;

    PaletteContext* palette;
    SceneSetup* sceneSetup;
    MMemStack* memStack;

#ifdef FINSPECTOR
    int logLevel;
    u32Array* loadedModelIndexes;
    ByteCodeTraceArray* byteCodeTrace;
#endif
} RenderContext;

MINLINE RenderFrame* GetRenderFrame(RenderContext* renderContext) {
    return renderContext->currentRenderFrame;
}

MINTERNAL RenderFrame* PushRenderFrame(RenderContext* renderContext) {
    renderContext->currentRenderFrameIx++;
    if (renderContext->currentRenderFrameIx >= RENDER_FRAMES_DEPTH) {
        renderContext->currentRenderFrameIx = RENDER_FRAMES_DEPTH - 1;
    }
    renderContext->currentRenderFrame = renderContext->renderFrame + renderContext->currentRenderFrameIx;
    return renderContext->currentRenderFrame;
}

MINTERNAL RenderFrame* PopRenderFrame(RenderContext* renderContext) {
    renderContext->currentRenderFrameIx--;
    if (renderContext->currentRenderFrameIx < 0) {
        renderContext->currentRenderFrameIx = 0;
    }
    renderContext->currentRenderFrame = renderContext->renderFrame + renderContext->currentRenderFrameIx;
    return renderContext->currentRenderFrame;
}

MINTERNAL void WriteDrawFunc(ZTree* zTree, u16 drawFunc) {
    *((u16 *)(zTree->data + zTree->offset)) = drawFunc;
#ifdef FINSPECTOR
    zTree->insOffset[zTree->offset] = zTree->insOffsetTmp;
#endif
    zTree->offset += 2;
}

MINTERNAL RasterOpNode* AddDrawNode(ZTree* zTree, i32 z, DrawFuncEnum drawFunc) {
    RasterOpNode* drawNode = ZTree_AddNode(zTree, z);
    drawNode->func.func = drawFunc;
    return drawNode;
}

#define ZNODE_ADD_FUNC(funcName, drawFuncEnum, ParamType) \
MINTERNAL ParamType* funcName(ZTree *zTree, i32 z) { \
    RasterOpNode* drawNode = ZTree_AddNode(zTree, z); \
    drawNode->func.func = drawFuncEnum; \
    ParamType* drawParams = (ParamType*)(zTree->data + zTree->offset); \
    zTree->offset += sizeof(ParamType); \
    return drawParams; \
}

#ifdef FINSPECTOR
#define ZNODE_APPEND_FUNC(funcName, drawFuncEnum, ParamType) \
MINTERNAL ParamType* funcName(ZTree *zTree) { \
    zTree->insOffset[zTree->offset] = zTree->insOffsetTmp; \
    u16* func = (u16*)(zTree->data + zTree->offset); \
    *func = drawFuncEnum; \
    zTree->offset += sizeof(u16); \
    ParamType* drawParams = (ParamType*)(zTree->data + zTree->offset); \
    zTree->offset += sizeof(ParamType); \
    return drawParams; \
}
#else
#define ZNODE_APPEND_FUNC(funcName, drawFuncEnum, ParamType) \
ParamType* funcName(ZTree *zTree) { \
    u16* func = (u16*)(zTree->data + zTree->offset); \
    *func = drawFuncEnum; \
    zTree->offset += sizeof(u16); \
    ParamType* drawParams = (ParamType*)(zTree->data + zTree->offset); \
    zTree->offset += sizeof(ParamType); \
    return drawParams; \
}
#endif

MINTERNAL u16* BatchSpanStart(ZTree *zTree) {
#ifdef FINSPECTOR
    zTree->insOffset[zTree->offset] = zTree->insOffsetTmp;
#endif
    u16* func = (u16*)(zTree->data + zTree->offset);
    *func = DRAW_FUNC_SPANS_START;
    zTree->offset += sizeof(u16);
    return func;
}

MINTERNAL u16* BatchBodySpanStart(ZTree *zTree) {
#ifdef FINSPECTOR
    zTree->insOffset[zTree->offset] = zTree->insOffsetTmp;
#endif
    u16* func = (u16*)(zTree->data + zTree->offset);
    *func = DRAW_FUNC_BODY_START;
    zTree->offset += sizeof(u16);
    return func;
}

ZNODE_APPEND_FUNC(BatchSpanEnd, DRAW_FUNC_SPANS_END, DrawParamsColour)
ZNODE_APPEND_FUNC(BatchSpanPoint, DRAW_FUNC_SPANS_POINT, DrawParamsPoint)
ZNODE_APPEND_FUNC(BatchSpanLine, DRAW_FUNC_SPANS_LINE, DrawParamsLine)
ZNODE_APPEND_FUNC(BatchSpanBezier, DRAW_FUNC_SPANS_BEZIER, DrawParamsBezier)
ZNODE_APPEND_FUNC(BatchSpanDraw, DRAW_FUNC_SPANS_DRAW, DrawParamsColour)
ZNODE_APPEND_FUNC(BatchSpanLineCont, DRAW_FUNC_SPANS_LINE_CONT, DrawParamsPoint)
ZNODE_APPEND_FUNC(BatchLine, DRAW_FUNC_LINE, DrawParamsLineColour)
ZNODE_APPEND_FUNC(BatchBezierLine, DRAW_FUNC_BEZIER_LINE, DrawParamsBezierColour)
ZNODE_APPEND_FUNC(BatchTri, DRAW_FUNC_TRI, DrawParamsTri)
ZNODE_APPEND_FUNC(BatchCircle, DRAW_FUNC_CIRCLE, DrawParamsCircle)
ZNODE_APPEND_FUNC(BatchBalls, DRAW_FUNC_BALLS, DrawParamsBalls)
ZNODE_APPEND_FUNC(BatchHighlight, DRAW_FUNC_FLARE, DrawParamsFlare)
ZNODE_APPEND_FUNC(BatchRingedCircle, DRAW_FUNC_RINGED_CIRCLE, DrawParamsRingedCircle)
ZNODE_APPEND_FUNC(BatchQuad, DRAW_FUNC_QUAD, DrawParamsQuad)
ZNODE_APPEND_FUNC(BatchBodyLine, DRAW_FUNC_BODY_LINE, DrawParamsLineColour)
ZNODE_APPEND_FUNC(BatchBodyBezier, DRAW_FUNC_BODY_BEZIER, DrawParamsBezierColour)
ZNODE_APPEND_FUNC(BatchBodyXor, DRAW_FUNC_BODY_XOR, DrawParamsBodyXor)
ZNODE_APPEND_FUNC(BatchBodyDraw1, DRAW_FUNC_BODY_DRAW_1, DrawParamsColour8)
ZNODE_APPEND_FUNC(BatchBodyDraw2, DRAW_FUNC_BODY_DRAW_2, DrawParamsColour16)
ZNODE_APPEND_FUNC(BatchBodyDraw3, DRAW_FUNC_BODY_DRAW_3, DrawParamsColour8)

ZNODE_ADD_FUNC(AddLineNode, DRAW_FUNC_LINE, DrawParamsLineColour)
ZNODE_ADD_FUNC(AddBezierLineNode, DRAW_FUNC_BEZIER_LINE, DrawParamsBezierColour)
ZNODE_ADD_FUNC(AddTriNode, DRAW_FUNC_TRI, DrawParamsTri)
ZNODE_ADD_FUNC(AddCircleNode, DRAW_FUNC_CIRCLE, DrawParamsCircle)
ZNODE_ADD_FUNC(AddBallsNode, DRAW_FUNC_BALLS, DrawParamsBalls)
ZNODE_ADD_FUNC(AddRingedCircleNode, DRAW_FUNC_RINGED_CIRCLE, DrawParamsRingedCircle)
ZNODE_ADD_FUNC(AddHighlightNode, DRAW_FUNC_FLARE, DrawParamsFlare)
ZNODE_ADD_FUNC(AddQuadNode, DRAW_FUNC_QUAD, DrawParamsQuad)

MINTERNAL Vec2i16 ScreenCoords(Vec2i16 pos) {
    pos.x = pos.x + (SURFACE_WIDTH >> 1);
    pos.y = (SURFACE_HEIGHT >> 1) - pos.y;
    return pos;
}

MINTERNAL Vec2i16 ZProject(i32 x, i32 y, i32 z) {
    i32 x1;
    i32 y1;

    if (z < (1 << ZSCALE)) {
        x1 = x << ZSCALE;
        y1 = y << ZSCALE;
    } else {
        i32 max = 0xffffffffu >> (ZSCALE + 1);
        i32 min = -max - 1;

        if (x > max || x < min) {
            x1 = (x / (z >> ZSCALE));
        } else {
            x1 = ((x << ZSCALE) / z);
        }
        if (y > max || y < min) {
            y1 = (y / (z >> ZSCALE));
        } else {
            y1 = ((y << ZSCALE) / z);
        }
    }

    if (x1 > 0) {
        while (x1 > (i32)0x1f00) {
            x1 >>= 1;
            y1 >>= 1;
        }
    } else {
        while (x1 < (i32)-0x1f00) {
            x1 >>= 1;
            y1 >>= 1;
        }
    }

    if (y1 > 0) {
        while (y1 > (i32)0x1f00) {
            x1 >>= 1;
            y1 >>= 1;
        }
    } else {
        while (y1 < (i32)-0x1f00) {
            x1 >>= 1;
            y1 >>= 1;
        }
    }

    Vec2i16 r = {(i16)x1, (i16)y1};
    return r;
}

MINTERNAL Vec2i16 ZProjecti32(const Vec3i32 p) {
    i32 x = p[0];
    i32 y = p[1];
    i32 z = p[2];

    return ZProject(x, y ,z);
}

MINTERNAL Vec2i16 ZProjecti16(const Vec3i16 p) {
    i32 x = p[0];
    i32 y = p[1];
    i32 z = p[2];

    return ZProject(x, y ,z);
}

MINTERNAL Vec2i16 ZProjectPoint(const Vec3i32 p) {
    return ScreenCoords(ZProjecti32(p));
}

MINTERNAL int IsInViewport(i32 radius, i32 x, i32 y, i32 z) {
    z += radius;
    if (z < 0) {
        return 0;
    }

    if (x < 0) {
        x = -x;
    }

    if (y < 0) {
        y = -y;
    }

    x -= radius;
    if (x < 0) {
        x = 0;
    }

    if (x > z) {
        return 0;
    }

    y -= radius;
    if (y < 0) {
        y = 0;
    }

    if (y > z) {
        return 0;
    }

    return 1;
}

// Get one of a constant, temporary variable, or scene value from an 8 bit parameter
// value.
// Returns a 16 bit value.
MINTERNAL u16 GetValueForParam8(RenderFrame* rf, u16 param8) {
    u16 cmd = param8 & 0xc0;
    u16 val = param8 & 0x3f;

    switch (cmd) {
        case 0x00:
            return val;
        case 0x40:
            return val << 10;
        case 0x80:
            return rf->renderContext->modelVars[val];
        case 0xc0:
        default:
            // this can alias values off the end of this struct?
#ifdef FINSPECTOR
            if (val > 7) {
                MLogf("Exceeded param Range: %d", val);
            }
#endif
            return rf->tmpVariable[val];
    }
}

// Get one of a constant, temporary variable, or scene value from a 16 bit parameter
// value.
// Returns a 16 bit value.
MINTERNAL u16 GetValueForParam16(RenderFrame* rf, u16 param16) {
    if (param16 & 0x80) {
        u16 val = param16 & 0x3f;
        if (param16 & 0x40) {
#ifdef FINSPECTOR
            if (val > 7) {
                MLogf("Exceeded param Range: %d", val);
            }
#endif
            // this can alias values off the end of this struct?
            return rf->tmpVariable[val];
        } else {
            return rf->renderContext->modelVars[val];
        }
    }

    // Translate bits so that the positions end up as:
    // 6 5 4 3 2 1 0 15 14 13 12 11 10 9 8 7
    // NOTE: due to the above checks bit 7 is always 0
    return (param16 << 9) | (param16 >> 7);
}

MINTERNAL int CheckClipped(RenderContext* objectContext, ModelData* modelData) {
    RenderFrame* rf = GetRenderFrame(objectContext);

    i32 radius = modelData->radius << rf->scale;
    return IsInViewport(radius, rf->objViewVec[0], rf->objViewVec[1], rf->objViewVec[2]);
}

MINTERNAL void ViewProjectVertex(RenderFrame* rf, VertexData* vertex) {
    Vec3i32Add(vertex->wVec, rf->objViewVec, vertex->vVec);

    if (vertex->projectedState < 0) {
        return;
    }

    if (vertex->vVec[2] < ZCLIPNEAR) {
        vertex->sVec.x = PT_ZCLIPPED;
        vertex->sVec.y = PT_ZCLIPPED;
        vertex->projectedState = rf->frameId;
        return;
    }

    vertex->sVec = ScreenCoords(ZProjecti32(vertex->vVec));
    vertex->projectedState = rf->frameId;
}

MINTERNAL i32 NextRandom(RenderContext* rc, u16 scale) {
    u32 r1 = rc->random1;
    u32 r2 = rc->random2;
    rc->random1 = r1 + r2;
    r1 = flipWords(r1);
    rc->random2 = r1;
    return ((i32)r1) >> scale;
}

MINTERNAL VertexData* TransformModelCoords(RenderContext *rc, RenderFrame* rf, i16 vertexIndex);

MINTERNAL VertexData* ProjectVertex(RenderContext *scene, RenderFrame* rf, i16 vertexIndex);

MINTERNAL VertexData* ProjectVertexRecursive(RenderContext* rc, RenderFrame* rf, i16 vertexIndex, i16 projectedState) {
    if (vertexIndex < 0) {
        i16 v1x = rf->parentFrameVertexIndexes[-(vertexIndex + 1)];
        VertexData* src = rf->parentVertexTrans + v1x;
        VertexData* dest = rf->vertexTrans + vertexIndex;
        dest->sVec = src->sVec;
        Vec3i32Sub(src->vVec, rf->objViewVec, dest->wVec);
        Vec3i32Copy(src->vVec, dest->vVec);
        return dest;
    }

    i16 vertexEvenIndex = (i16)((u16)vertexIndex & (u16)0xfffe);

    u16* vertexData = rf->vertexData;

    // 1 byte type, 3 bytes for (x, y, z)
    u16 vertexData1 = vertexData[vertexEvenIndex];
    u16 vertexData2 = vertexData[vertexEvenIndex + 1];

    u8 vertexType = vertexData1 >> 8;

    switch (vertexType) {
        case 0x00:
        case 0x01:
        case 0x02: {
            // Regular vertex perspective projection
            // Two vertices are transformed, only the one asked for is perspective projected
            VertexData* v1 = rf->vertexTrans + vertexEvenIndex;
            VertexData* v2 = v1 + 1;
            v1->projectedState = projectedState;
            v2->projectedState = projectedState;

            Vec3i32 v;
            v[0] = lo8s32(vertexData1) << 8;
            v[1] = hi8s32(vertexData2) << 8;
            v[2] = lo8s32(vertexData2) << 8;

            i32 dx = rf->modelViewMatrix[0][0] * v[0];
            i32 dy = rf->modelViewMatrix[0][1] * v[0];
            i32 dz = rf->modelViewMatrix[0][2] * v[0];

            i32 x = dx + rf->modelViewMatrix[1][0] * v[1] + rf->modelViewMatrix[2][0] * v[2];
            i32 y = dy + rf->modelViewMatrix[1][1] * v[1] + rf->modelViewMatrix[2][1] * v[2];
            i32 z = dz + rf->modelViewMatrix[1][2] * v[1] + rf->modelViewMatrix[2][2] * v[2];

            u16 scale = (u16)0x17 - rf->scale;

            x >>= scale;
            y >>= scale;
            z >>= scale;
            dx >>= scale;
            dy >>= scale;
            dz >>= scale;

            v1->wVec[0] = x;
            v1->wVec[1] = y;
            v1->wVec[2] = z;

            v2->wVec[0] = x - (dx * 2);
            v2->wVec[1] = y - (dy * 2);
            v2->wVec[2] = z - (dz * 2);

            VertexData* vOrig = rf->vertexTrans + vertexIndex;
            ViewProjectVertex(rf, vOrig);
            return vOrig;
        }
        case 0x03:
        case 0x04: {
            // screenspace average of two projected vertices (project them first if needed)
            // do for requested vertex and its sibling
            i16 v1i = vertexEvenIndex;
            i16 v2i = vertexEvenIndex + 1;

            VertexData* v1 = rf->vertexTrans + v1i;
            VertexData* v2 = rf->vertexTrans + v2i;
            v1->projectedState = rf->frameId;
            v2->projectedState = rf->frameId;

            i16 v3i = hi8s(vertexData2);
            VertexData* v3 = ProjectVertex(rc, rf, v3i);

            i16 v4i = lo8s(vertexData2);
            VertexData* v4 = ProjectVertex(rc, rf, v4i);

            i32 x = ((i32)v3->sVec.x + (i32)v4->sVec.x) >> 1;
            i32 y = ((i32)v3->sVec.y + (i32)v4->sVec.y) >> 1;

            v1->sVec.x = (i16)x;
            v1->sVec.y = (i16)y;

            i32 z = v3->vVec[2];
            if (v4->vVec[2] > z) {
                z = v4->vVec[2];
            }

            v1->vVec[2] = z;

            vertexData2 = vertexData2 ^ 0x101;

            i16 v5i = hi8s(vertexData2);
            VertexData* v5 = ProjectVertex(rc, rf, v5i);

            i16 v6i = lo8s(vertexData2);
            VertexData* v6 = ProjectVertex(rc, rf, v6i);

            x = ((i32)v5->sVec.x + (i32)v6->sVec.x) >> 1;
            y = ((i32)v5->sVec.y + (i32)v6->sVec.y) >> 1;

            v2->sVec.x = (i16)x;
            v2->sVec.y = (i16)y;

            z = v5->vVec[2];
            if (v6->vVec[2] > z) {
                z = v6->vVec[2];
            }

            v2->vVec[2] = z;
            break;
        }
        case 0x05:
        case 0x06: {
            // Negate vertex (x,y,z) and project
            i16 v1i = vertexEvenIndex;
            i16 v2i = vertexEvenIndex + 1;
            i16 v3i = lo8s(vertexData2);
            i16 v4i = v3i ^ 0x1;

            VertexData* v1 = rf->vertexTrans + v1i;
            VertexData* v2 = rf->vertexTrans + v2i;

            v1->projectedState = projectedState;
            v2->projectedState = projectedState;

            VertexData* v3 = rf->vertexTrans + v3i;
            if (v3->projectedState == 0) {
                TransformModelCoords(rc, rf, v3i);
            }
            v1->wVec[0] = -v3->wVec[0];
            v1->wVec[1] = -v3->wVec[1];
            v1->wVec[2] = -v3->wVec[2];

            VertexData* v4 = rf->vertexTrans + v4i;
            if (v4->projectedState == 0) {
                TransformModelCoords(rc, rf, v4i);
            }
            v2->wVec[0] = -v4->wVec[0];
            v2->wVec[1] = -v4->wVec[1];
            v2->wVec[2] = -v4->wVec[2];

            VertexData* vOrig = rf->vertexTrans + vertexIndex;
            ViewProjectVertex(rf, vOrig);
            return vOrig;
        }
        case 0x07:
        case 0x08: {
            // Add random vector of given size to vertex and project
            i16 v1i = vertexEvenIndex;
            i16 v2i = vertexEvenIndex + 1;
            i16 v3i = lo8s(vertexData2);
            i16 v4i = v3i ^ 0x1;

            VertexData* v1 = rf->vertexTrans + v1i;
            VertexData* v2 = rf->vertexTrans + v2i;

            v1->projectedState = projectedState;
            v2->projectedState = projectedState;

            VertexData* v3 = rf->vertexTrans + v3i;
            if (v3->projectedState == 0) {
                TransformModelCoords(rc, rf, v3i);
            }

            u16 scale = (vertexData1 & 0xff);
            i32 rx = NextRandom(rc, scale);
            i32 ry = NextRandom(rc, scale);
            i32 rz = NextRandom(rc, scale);

            v1->wVec[0] = v3->wVec[0] + rx;
            v1->wVec[1] = v3->wVec[1] + ry;
            v1->wVec[2] = v3->wVec[2] + rz;

            VertexData* v4 = rf->vertexTrans + v4i;
            if (v4->projectedState == 0) {
                TransformModelCoords(rc, rf, v4i);
            }

            v2->wVec[0] = v4->wVec[0] + rz;
            v2->wVec[1] = v4->wVec[1] + rx;
            v2->wVec[2] = v4->wVec[2] + ry;

            VertexData* vOrig = rf->vertexTrans + vertexIndex;
            ViewProjectVertex(rf, vOrig);
            return vOrig;
        }
        case 0x09:
        case 0x0a: {
            // Like regular vertex perspective projection only the second point's x coordinate is treated as zero
            // Two vertices are transformed, only the one asked for is perspective projected
            VertexData* v1 = rf->vertexTrans + vertexEvenIndex;
            VertexData* v2 = v1 + 1;
            v1->projectedState = projectedState;
            v2->projectedState = projectedState;

            Vec3i32 v;
            v[0] = lo8s32(vertexData1) << 8;
            v[1] = hi8s32(vertexData2) << 8;
            v[2] = lo8s32(vertexData2) << 8;

            i32 dx = rf->modelViewMatrix[0][0] * v[0];
            i32 dy = rf->modelViewMatrix[0][1] * v[0];
            i32 dz = rf->modelViewMatrix[0][2] * v[0];

            i32 x = dx + rf->modelViewMatrix[1][0] * v[1] + rf->modelViewMatrix[2][0] * v[2];
            i32 y = dy + rf->modelViewMatrix[1][1] * v[1] + rf->modelViewMatrix[2][1] * v[2];
            i32 z = dz + rf->modelViewMatrix[1][2] * v[1] + rf->modelViewMatrix[2][2] * v[2];

            u16 scale = (u16) 0x17 - rf->scale;

            x >>= scale;
            y >>= scale;
            z >>= scale;
            dx >>= scale;
            dy >>= scale;
            dz >>= scale;

            v1->wVec[0] = x;
            v1->wVec[1] = y;
            v1->wVec[2] = z;

            v2->wVec[0] = x - dx;
            v2->wVec[1] = y - dy;
            v2->wVec[2] = z - dz;

            VertexData* vOrig = rf->vertexTrans + vertexIndex;
            ViewProjectVertex(rf, vOrig);
            return vOrig;
        }
        default: {
            if (vertexIndex != vertexEvenIndex) {
                vertexData2 ^= 0x0101;
            }

            VertexData* vOrig = rf->vertexTrans + vertexIndex;
            vOrig->projectedState = projectedState;
            switch (vertexType) {
                case 0x0b:
                case 0x0c: {
                    // Transform two other vertices and project the average
                    i16 v1i = hi8s(vertexData2);
                    VertexData* vertex1 = rf->vertexTrans + v1i;
                    if (vertex1->projectedState == 0) {
                        TransformModelCoords(rc, rf, v1i);
                    }

                    i16 v2i = lo8s(vertexData2);
                    VertexData* vertex2 = rf->vertexTrans + v2i;
                    if (vertex2->projectedState == 0) {
                        TransformModelCoords(rc, rf, v2i);
                    }

                    vOrig->wVec[0] = (vertex1->wVec[0] + vertex2->wVec[0]) / 2;
                    vOrig->wVec[1] = (vertex1->wVec[1] + vertex2->wVec[1]) / 2;
                    vOrig->wVec[2] = (vertex1->wVec[2] + vertex2->wVec[2]) / 2;

                    ViewProjectVertex(rf, vOrig);
                    return vOrig;
                }
                case 0x0d:
                case 0x0e: {
                    // Screen space average of two projected pointed (project them first if needed)
                    // Don't do for sibling vertex.
                    i16 v1i = hi8s(vertexData2);
                    VertexData* v1 = ProjectVertex(rc, rf, v1i);

                    i16 v2i = lo8s(vertexData2);
                    VertexData* v2 = ProjectVertex(rc, rf, v2i);

                    i32 z = v1->vVec[2];

                    if (v1->vVec[2] < ZCLIPNEAR || v2->vVec[2] < ZCLIPNEAR) {
                        vOrig->sVec.x = PT_ZCLIPPED;
                        vOrig->sVec.y = PT_ZCLIPPED;
                        if (v2->vVec[2] < z) {
                            z = v2->vVec[2];
                        }
                    } else {
                        i32 x = ((i32)v1->sVec.x + (i32)v2->sVec.x) >> 1;
                        i32 y = ((i32)v1->sVec.y + (i32)v2->sVec.y) >> 1;

                        vOrig->sVec.x = (i16)x;
                        vOrig->sVec.y = (i16)y;

                        if (v2->vVec[2] > z) {
                            z = v2->vVec[2];
                        }
                    }

                    vOrig->vVec[0] = v2->vVec[0];
                    vOrig->vVec[1] = v2->vVec[1];
                    vOrig->vVec[2] = z;
                    vOrig->projectedState = rf->frameId;
                    return vOrig;
                }
                case 0x0f:
                case 0x10: {
                    // Add two vertices and subtract another, project if needed.
                    i16 v1i = hi8s(vertexData2);
                    VertexData* vertex1 = rf->vertexTrans + v1i;
                    if (vertex1->projectedState == 0) {
                        TransformModelCoords(rc, rf, v1i);
                    }

                    i16 v2i = lo8s(vertexData2);
                    VertexData* vertex2 = rf->vertexTrans + v2i;
                    if (vertex2->projectedState == 0) {
                        TransformModelCoords(rc, rf, v2i);
                    }

                    i16 v3i = lo8s(vertexData1);
                    VertexData* vertex3 = rf->vertexTrans + v3i;
                    if (vertex3->projectedState == 0) {
                        TransformModelCoords(rc, rf, v3i);
                    }

                    vOrig->wVec[0] = (vertex1->wVec[0] + vertex2->wVec[0] - vertex3->wVec[0]);
                    vOrig->wVec[1] = (vertex1->wVec[1] + vertex2->wVec[1] - vertex3->wVec[1]);
                    vOrig->wVec[2] = (vertex1->wVec[2] + vertex2->wVec[2] - vertex3->wVec[2]);

                    ViewProjectVertex(rf, vOrig);
                    return vOrig;
                }
                case 0x11:
                case 0x12: {
                    // Add two vertices and project if needed
                    i16 v1i = lo8s(vertexData2);
                    VertexData* vertex1 = rf->vertexTrans + v1i;
                    if (vertex1->projectedState == 0) {
                        TransformModelCoords(rc, rf, v1i);
                    }

                    i16 v2i = hi8s(vertexData2);
                    VertexData* vertex2 = rf->vertexTrans + v2i;
                    if (vertex2->projectedState == 0) {
                        TransformModelCoords(rc, rf, v2i);
                    }

                    vOrig->wVec[0] = (vertex1->wVec[0] + vertex2->wVec[0]);
                    vOrig->wVec[1] = (vertex1->wVec[1] + vertex2->wVec[1]);
                    vOrig->wVec[2] = (vertex1->wVec[2] + vertex2->wVec[2]);

                    ViewProjectVertex(rf, vOrig);
                    break;
                }
                case 0x13:
                case 0x14: {
                    // Lerp between two vertices
                    i16 v1i = hi8s(vertexData2);
                    VertexData* vertex1 = ProjectVertex(rc, rf, v1i);

                    i16 v2i = lo8s(vertexData2);
                    VertexData* vertex2 = ProjectVertex(rc, rf, v2i);

                    u16 p1 = GetValueForParam8(rf, vertexData1 & 0xff);
                    p1 >>= 1;

                    Vec3i32 vNew;
                    Vec3i32Sub(vertex2->wVec, vertex1->wVec, vNew);

                    i8 cl = GetScaleBelow(vNew, 0x3fff);
                    Vec3i32ShiftRight(vNew, cl);

                    vNew[0] = vNew[0] * p1;
                    vNew[1] = vNew[1] * p1;
                    vNew[2] = vNew[2] * p1;

                    cl -= 0xf;

                    if (cl > 0) {
                        Vec3i32ShiftLeft(vNew, cl);
                    } else if (cl < 0) {
                        cl = -cl;
                        Vec3i32ShiftRight(vNew, cl);
                    }

                    Vec3i32Add(vNew, vertex1->wVec, vOrig->wVec);

                    ViewProjectVertex(rf, vOrig);
                    return vOrig;
                }
                case 0x15:
                case 0x16: {
                    MLogf("Unhandled vertex type %x", vertexType);
                    i16 v1i = hi8s(vertexData2);
                    if (v1i == 0) {
                        // not sure what this one is...
                    }
                    break;
                }
                default: {
                    MLogf("Vertex type out of range %x", vertexType);
                    break;
                }
            }
        }
    }

    return rf->vertexTrans + vertexIndex;
}

MINTERNAL VertexData* TransformVertex(RenderContext* rc, RenderFrame* rf, i16 vertexIndex) {
    VertexData* v1 = rf->vertexTrans + vertexIndex;
    if (v1->projectedState == 0) {
        return TransformModelCoords(rc, rf, vertexIndex);
    } else {
        Vec3i32Add(v1->wVec, rf->objViewVec, v1->vVec);
        return v1;
    }
}

MINTERNAL void ProjectVertexDirect(RenderFrame* rf, VertexData* vertex) {
    Vec3i32Add(vertex->wVec, rf->objViewVec, vertex->vVec);

    if (vertex->vVec[2] < ZCLIPNEAR) {
        vertex->sVec.x = PT_ZCLIPPED;
        vertex->sVec.y = PT_ZCLIPPED;
        vertex->projectedState = rf->frameId;
        return;
    }

    vertex->sVec = ScreenCoords(ZProjecti32(vertex->vVec));
    vertex->projectedState = rf->frameId;
}

MINTERNAL VertexData* ProjectVertex(RenderContext* rc, RenderFrame* rf, i16 vertexIndex) {
    VertexData* vertex = rf->vertexTrans + vertexIndex;
    i16 projectionMode = vertex->projectedState;
    if (projectionMode != rf->frameId) {
        if (projectionMode) {
            ProjectVertexDirect(rf, vertex);
        } else {
            ProjectVertexRecursive(rc, rf, vertexIndex, 1);
        }
    }
    return vertex;
}

MINTERNAL VertexData* TransformModelCoords(RenderContext* rc, RenderFrame* rf, i16 vertexIndex) {
    return ProjectVertexRecursive(rc, rf, vertexIndex, -1);
}

MINTERNAL FontModelData* GetFontModel(SceneSetup* sceneSetup, u16 offset) {
    return (FontModelData*)MArrayGet(sceneSetup->assets.fontModels, offset);
}

MINTERNAL u8* GetFontByteCodeForCharacter(FontModelData* fontModel, u16 offset) {
    u8* base = (u8*)(fontModel);
    return (base + fontModel->offsets[offset]);
}

// neg light indicates backface (not facing view vec)
// zero light indicates not facing light source
// NOTE: normals have a vertex associated with them, which should have already been transformed
MINTERNAL u16 CalcNormalLightTint(RenderFrame* rf, u8 normalIndex) {
    u16 normalOffset = rf->normalOffset + ((normalIndex & 0x7e) << 1);

    i16* normalDataPtr = (i16*)(((u8*)rf->modelData) + normalOffset);

    u16 nPacked1 = *(normalDataPtr - 2);
    u16 nPacked2 = *(normalDataPtr - 1);

    Vec3i32 n;
    n[0] = lo8s(nPacked1) << 8;
    n[1] = (i16)(nPacked2 & 0xff00);
    n[2] = lo8s(nPacked2) << 8;

    u16 vi = (nPacked1 >> 8);
    u16 viDataIndex = vi & 0xfffe;

    u16 vPacked1 = rf->vertexData[viDataIndex];
    u16 vPacked2 = rf->vertexData[viDataIndex + 1];

    // sign extend x,y,z
    Vec3i32 v;
    v[0] = lo8s(vPacked1);
    v[1] = hi8s(vPacked2);
    v[2] = lo8s(vPacked2);

    // Both odd vertex and normals have their x axis reversed
    if (normalIndex & 0x1) {
        n[0] = -n[0];
        if (!(vi & 0x1)) {
            v[0] = -v[0];
        }
    } else {
        if (vi & 0x1) {
            v[0] = -v[0];
        }
    }

    i16 p2 = rf->scale - rf->objViewVecScale;
    if (p2 > 0) {
        v[0] <<= p2;
        v[1] <<= p2;
        v[2] <<= p2;
    } else if (p2 < 0) {
        v[0] >>= -p2;
        v[1] >>= -p2;
        v[2] >>= -p2;
    }

    // View vector
    v[0] -= rf->viewM[0];
    v[1] -= rf->viewM[1];
    v[2] -= rf->viewM[2];

    i32 viewDot = Vec3i32DotProd(v, n);
    if (viewDot >= 0) {
        return NORMAL_FACING_AWAY;
    }

    i32 lightDot = Vec3i16i32DotProd(rf->lightM, n);
    if (lightDot >= 0) {
        return 0;
    }

    u16 colour = (lightDot >> 27) & 0x7;
    return rf->shadeRamp[colour];
}

#ifdef NON_TRICKY_POINT_CHECK
MINTERNAL bool IsPointVisible(i16 x, i16 y, u16 w, u16 h) {
    return (x < w && x >= 0 && y < h && y >= 0);
}
#else
MINTERNAL b32 IsPointVisible(i16 x, i16 y, u16 w, u16 h) {
    return ((u16)x) < w && ((u16)y) < h;
}
#endif

MINTERNAL b32 IsCircleVisible(i16 x, i16 y, i16 r, u16 width, u16 height) {
    if (x + r < 0) {
        return FALSE;
    }

    if (y + r < 0) {
        return FALSE;
    }

    if (x - r >= width) {
        return FALSE;
    }

    return (y - r) < height;
}

MINTERNAL Vec2i16 ClipLineZVec3i32(const Vec3i32 v1, const Vec3i32 v2, u16 width, u16 height) {
    i32 x1 = v1[0];
    i32 y1 = v1[1];
    i32 z1 = v1[2];

    i32 dx = (v2[0] - x1);
    i32 dy = (v2[1] - y1);
    i32 dz = (v2[2] - z1);

    i64 d = ZCLIPNEAR - z1;
    i64 x3, y3;
    if (dz != 0) {
        x3 = x1 + ((dx * d) / dz);
        y3 = y1 + ((dy * d) / dz);
    } else {
        x3 = 0;
        y3 = 0;
    }

    if (x3 > 0x3fff) {
        y3 = (y3 * 0x3fff) / x3;
        x3 = 0x3fff;
    } else if (x3 < -0x4000) {
        y3 = (y3 * -0x4000) / x3;
        x3 = -0x4000;
    }

    if (y3 > 0x3fff) {
        x3 = (x3 * 0x3fff) / y3;
        y3 = 0x3fff;
    } else if (y3 < -0x4000) {
        x3 = (x3 * -0x4000) / y3;
        y3 = -0x4000;
    }

    Vec2i16 pos;
    pos.x = x3;
    pos.y = y3;

    pos.x = ((i16)pos.x) + (width >> 1);
    pos.y = (height >> 1) - ((i16)pos.y);

    return pos;
}

MINTERNAL Vec2i16 ClipLineZ(VertexData* v1, VertexData* v2, u16 width, u16 height) {
    return ClipLineZVec3i32(v1->vVec, v2->vVec, width, height);
}

static u16 sMatrixAxisFlips[] = {
        // Winding,
        //      X Flipped with
        //              Y Flipped with
        //                      Z Flipped with
        0x0000, 0x0000, 0x0001, 0x0002,  // x y z  - leave alone
        0x0000, 0x0001, 0x0002, 0x0000,  // y z x
        0x0000, 0x0002, 0x0000, 0x0001,  // z x y
        0xffff, 0x0002, 0x0001, 0x0000,  // z y x  - winding reversed
        0xffff, 0x0001, 0x0000, 0x0002,  // y x z  - winding reversed
        0xffff, 0x0000, 0x0002, 0x0001,  // x z y
        0x0000, 0x0000, 0x0001, 0x0002,  // x y z  - NOP
        0x0000, 0x0000, 0x0001, 0x0002   // x y z  - NOP
};

static u16 sXyzRotTableOffsets[] = {
    1, 2, 0, 1
};

static u16 sMatrixOffsets3[] = {
    1, 2, 0, 0
};

MINTERNAL i16 FlipMatrixAxis(RenderFrame* rf, Matrix3x3i16 matrixDest, u16 flipCommand) {
    u16 flipOffset = (flipCommand & 0x7) << 2;

    i16 newWinding = rf->matrixWinding ^ sMatrixAxisFlips[flipOffset];

    i16 x = rf->modelViewMatrix[0][0];
    i16 y = rf->modelViewMatrix[0][1];
    i16 z = rf->modelViewMatrix[0][2];

    if (flipCommand & 0x20) {
        x = -x;
        y = -y;
        z = -z;
        newWinding = ~newWinding;
    }

    u16 di = sMatrixAxisFlips[flipOffset + 1];
    matrixDest[di][0] = x;
    matrixDest[di][1] = y;
    matrixDest[di][2] = z;

    x = rf->modelViewMatrix[1][0];
    y = rf->modelViewMatrix[1][1];
    z = rf->modelViewMatrix[1][2];

    if (flipCommand & 0x10) {
        x = -x;
        y = -y;
        z = -z;
        newWinding = ~newWinding;
    }

    di = sMatrixAxisFlips[flipOffset + 2];
    matrixDest[di][0] = x;
    matrixDest[di][1] = y;
    matrixDest[di][2] = z;

    x = rf->modelViewMatrix[2][0];
    y = rf->modelViewMatrix[2][1];
    z = rf->modelViewMatrix[2][2];

    if (flipCommand & 0x08) {
        x = -x;
        y = -y;
        z = -z;
        newWinding = ~newWinding;
    }

    di = sMatrixAxisFlips[flipOffset + 3];
    matrixDest[di][0] = x;
    matrixDest[di][1] = y;
    matrixDest[di][2] = z;

    return newWinding;
}

MINTERNAL void TransformLightAndViewVectors(RenderContext *scene) {
    RenderFrame* rf = GetRenderFrame(scene);

    i8 s = GetScaleBelow(rf->objViewVec, 0x3f00);

    Vec3i32 v;
    Vec3i32Copy(rf->objViewVec, v);
    Vec3i32ShiftRight(v, s);

    rf->objViewVecScale = s;

    s = s + 7 - rf->scale;
    if (s < 0) {
        s = -s;
        rf->objViewVecScale += s;

        Vec3i32ShiftRight(v, s);
    }

    Vec3i32Neg(v);

    MultVec3i32Vec3i16Matrix(rf->modelViewMatrix, v, rf->viewM);
    MultVec3i16Matrix(rf->modelViewMatrix, rf->lightV, rf->lightM);
}

MINTERNAL int SetupNewTransformMatrix(RenderContext* renderContext, RenderFrame* newRenderFrame, RenderFrame* prevRenderFrame, u16 param) {
    u16 flipOffset = (param & (u16)0x7);
    if (flipOffset == 6) {
        Matrix3i16Copy(prevRenderFrame->modelMatrix, newRenderFrame->modelViewMatrix);
        newRenderFrame->matrixWinding = prevRenderFrame->newMatrixWinding;
    } else {
        newRenderFrame->matrixWinding = FlipMatrixAxis(prevRenderFrame, newRenderFrame->modelViewMatrix, param);
    }

    Vec3i16Copy(prevRenderFrame->lightV, newRenderFrame->lightV);

    // copy colour ramp
    for (int i = 0; i < 8; i++) {
        newRenderFrame->shadeRamp[i] = prevRenderFrame->shadeRamp[i];
    }

    if (!(param & 0x4000)) {
        TransformLightAndViewVectors(renderContext);
    }

    return 0;
}

MINTERNAL void FrameRenderObjects(RenderContext* rc, ModelData* model);

// Calc the vertex 12 bit colour.
// Only 9 bits (3red/3green/3blue) are used for the colour
//
// r & 0x1    - Emit colour directly / don't add normal colour
// g & 0x1    - Add the scene tint colour
// b & 0x1    - Is usually masked out / left out of bytecode format
MINTERNAL u16 CalcNormalColour(RenderFrame* rf, u16 colourParam, u8 normalIndex) {
    u16* n = rf->normals + normalIndex;
    u16 normalLightTint = *n;
    if (normalLightTint == NORMAL_NOT_CALCULATED) {
        normalLightTint = CalcNormalLightTint(rf, normalIndex);
    }

    *n = normalLightTint;

    if (normalLightTint & 0x8000) {
        return NORMAL_FACING_AWAY;
    }

    u16 colour = (colourParam & (u16)0xeee);

    if (!(colourParam & 0x100)) {
        colour += normalLightTint;
    }

    if (colourParam & 0x10) {
        colour += rf->baseColour;
    }

    return colour;
}

// Calc the vertex 12 bit colour.
// Only 9 bits are used for the colour, the rest are flags.
//
// g & 0x1    - Add the scene tint colour
// !(b & 0x1) - Add the normal colour
MINTERNAL u16 CalcColour(RenderFrame* rf, u16 colourParam, u8 normalLightTint) {
    u16 colour = (colourParam & (u16)0xeee);

    if (!(colourParam & 0x100)) {
        colour += rf->shadeRamp[normalLightTint];
    }

    if (colourParam & 0x10) {
        colour += rf->baseColour;
    }

    return colour;
}

MINTERNAL u16 CalcColourNoShade(RenderFrame* rf, u16 colourParam) {
    u16 colour = (colourParam & (u16)0xeee);

    if (colourParam & 0x10) {
        colour += rf->baseColour;
    }

    return colour;
}

MINTERNAL u16 CalcColourNoShadeNoBase(RenderFrame* rf, u16 colourParam) {
    return (colourParam & (u16)0xefe);
}

MINTERNAL u16 CalcNewBaseColour(RenderFrame* rf, u16 colourParam) {
    u16 colour = (colourParam & (u16)0xeee);

    if (colourParam & 0x10) {
        colour += rf->baseColour;
    }

    return colour;
}

MINTERNAL u16 CalcColourMidTint(RenderFrame* rf, u16 colourParam) {
    return CalcColour(rf, colourParam, 3);
}

MINTERNAL void NoOpLastDrawBatch(RenderContext* scene) {
    scene->lastBatchFunc->func = DRAW_FUNC_NOP;
    scene->zTree->offset = scene->lastBatchOffset;
}

MINTERNAL void SkipRemainingSpanFuncs(RenderContext* renderContext, RenderFrame* rf) {
    while (!ByteCodeIsDone(rf)) {
        u16 func = ByteCodeRead16u(rf);
        u8 funcOffset = ((func >> 9) & 0x7);
        switch (funcOffset) {
            case RComplexFunc_Done:
                NoOpLastDrawBatch(renderContext);
                return;
            case RComplexFunc_Bezier:
            case RComplexFunc_Circle:
                ByteCodeSkipBytes(rf, 4);
                break;
            case RComplexFunc_BezierCont:
            case RComplexFunc_Line:
                ByteCodeSkipBytes(rf, 2);
                break;
            case RComplexFunc_LineCont:
            case RComplexFunc_LineJoin:
                break;
            default:
                return;
        }
    }
}

MINTERNAL i32 ClipSpanLine(RenderContext* renderContext, VertexData* v1, VertexData* v2, i16 vi2) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    rf->complexLastVertexIx = vi2;
    rf->complexJoinToBeginning = 1;

    if (v1->vVec[2] < ZCLIPNEAR) {
        if (rf->complexSkipIfPointZClipped) {
            SkipRemainingSpanFuncs(renderContext, rf);
            return -1;
        } else {
            if (v2->vVec[2] < ZCLIPNEAR) {
                rf->complexJoinToBeginning = 0;
                rf->complexCurrentlyZClipped = 1;
            } else {
                Vec2i16 v1Clip = ClipLineZ(v2, v1, renderContext->width, renderContext->height);
                rf->complexLastZClipPt = v1Clip;
                DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
                drawLine->x1 = v1Clip.x;
                drawLine->y1 = v1Clip.y;
                drawLine->x2 = v2->sVec.x;
                drawLine->y2 = v2->sVec.y;
            }
        }
    } else if (v2->vVec[2] < ZCLIPNEAR) {
        Vec2i16 v2Clip = ClipLineZ(v1, v2, renderContext->width, renderContext->height);
        rf->complexCurrentlyZClipped = 1;
        DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
        drawLine->x1 = v1->sVec.x;
        drawLine->y1 = v1->sVec.y;
        drawLine->x2 = v2Clip.x;
        drawLine->y2 = v2Clip.y;
    } else {
        rf->complexCurrentlyZClipped = 0;
        DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
        drawLine->x1 = v1->sVec.x;
        drawLine->y1 = v1->sVec.y;
        drawLine->x2 = v2->sVec.x;
        drawLine->y2 = v2->sVec.y;
    }

    return 0;
}

MINTERNAL i32 CalcZVec3i32Offset(RenderFrame* rf, const Vec3i32 v, i32 z) {
    i16 scale = 3 - rf->zScale;
    z += v[2];
    if (scale < 0) {
        scale += 0x18;
        if (z >= 0) {
            scale = scale & 0x3f;
            if (scale) {
                z >>= scale;
            }
            z = z | 0x80000000;
        }
    } else {
        i32 x = v[0];
        if (x < 0) {
            x = -x;
        }

        i32 y = v[1];
        if (y < 0) {
            x -= y;
        } else {
            x += y;
        }

        x >>= 3;

        z = x + z;

        if (z < 0) {
            return 1;
        }

        if (scale >= 0) {
            z >>= scale;
        }

        if (!z) {
            return 1;
        }
    }

    return z;
}

MINTERNAL i32 CalcZ(RenderFrame* rf, i32 z) {
    i16 aa = 3 - rf->zScale;
    z += rf->objViewVec[2];
    if (aa < 0) {
        aa += 0x18;
        if (z > 0) {
            aa = aa & 0x3F;
            if (aa) {
                z >>= aa;
            }
            z = z | 0x80000000;
        }
    }

    i32 x = rf->objViewVec[0];
    if (x < 0) {
        x = -x;
    }

    i32 y = rf->objViewVec[1];
    if (y < 0) {
        x -= y;
    } else {
        x += y;
    }

    x >>= 3;

    z = x + z;

    if (z < 0) {
        return 1;
    }

    if (aa >= 0) {
        z >>= aa;
    }

    if (!z) {
        return 1;
    }

    return z;
}

MINTERNAL i32 CalcZ0(RenderFrame* rf) {
    return CalcZ(rf, 0);
}

MINTERNAL i32 CalcZVec3i32(RenderFrame* rf, const Vec3i32 v) {
    i16 scale = 3 - rf->zScale;
    i32 z = v[2];
    if (scale < 0) {
        scale += 0x18;
        if (z >= 0) {
            scale = scale & 0x3f;
            if (scale) {
                z >>= scale;
            }
            z = z | 0x80000000;
        }
    } else {
        i32 y = v[1];
        if (y < 0) {
            y = -y;
        }

        i32 x = v[0];
        if (x < 0) {
            y -= x;
        } else {
            y += x;
        }

        y >>= 3;

        z = y + z;

        if (z < 0) {
            return 1;
        }

        if (scale >= 0) {
            z >>= scale;
        }

        if (!z) {
            return 1;
        }
    }

    return z;
}

static void RenderLineWithVerts(RenderContext* scene, RenderFrame* rf, VertexData* v1, VertexData* v2, u16 colour) {
    Vec2i16 v1Clipped = v1->sVec;
    Vec2i16 v2Clipped = v2->sVec;

    if (!IsPointVisible(v1->sVec.x, v1->sVec.y, scene->width, scene->height)) {
        if (!IsPointVisible(v2->sVec.x, v2->sVec.y, scene->width, scene->height)) {

            u8 clipBits = 0;

            clipBits = UpdateClipBits(v1->sVec.x, clipBits, scene->width);
            clipBits = UpdateClipBits(v2->sVec.x, clipBits, scene->width);

            if (sClipLookupTable[clipBits]) {
                return;
            }

            clipBits = 0;

            clipBits = UpdateClipBits(v1->sVec.y, clipBits, scene->height);
            clipBits = UpdateClipBits(v2->sVec.y, clipBits, scene->height);

            if (sClipLookupTable[clipBits]) {
                return;
            }
        }

        // Both points could be zclipped, technically we could re-run the early-out code above again, but the
        // rastorizor will get that case anyway.
        if (v2->vVec[2] < ZCLIPNEAR) {
            if (v1->vVec[2] >= ZCLIPNEAR) {
                v2Clipped = ClipLineZ(v2, v1, scene->width, scene->height);
            } else {
                // both points behind z clip plane
                return;
            }
        } else {
            if (v1->vVec[2] < ZCLIPNEAR) {
                v1Clipped = ClipLineZ(v1, v2, scene->width, scene->height);
            }
        }
    } else {
        if (!IsPointVisible(v2->sVec.x, v2->sVec.y, scene->width, scene->height)) {
            // In this case only v2 can be z-clipped
            if (v2->vVec[2] < ZCLIPNEAR) {
                v2Clipped = ClipLineZ(v2, v1, scene->width, scene->height);
            }
        }
    }

    DrawParamsLineColour *line = NULL;
    if (scene->currentBatchId) {
        line = BatchLine(scene->zTree);
    } else {
        i32 z = CalcZVec3i32(rf, v1->vVec);
        line = AddLineNode(scene->zTree, z);
    }

    line->x1 = v1Clipped.x;
    line->y1 = v1Clipped.y;
    line->x2 = v2Clipped.x;
    line->y2 = v2Clipped.y;
    line->colour = Palette_GetIndexFor12bitColour(scene->palette, colour);
}

static void RenderLineParams(RenderContext* renderContext, RenderFrame* rf, u16 param0, u16 param1) {
    u16 colourParam = param0 >> 4;
    u16 colour = CalcColourMidTint(rf, colourParam);
    if (colour & 0x8000) {
        return;
    }

    i16 vi1 = lo8s(param1);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);

    i16 vi2 = hi8s(param1);
    VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

    RenderLineWithVerts(renderContext, rf, v1, v2, colour);
}

MINTERNAL void RenderTriVerts(RenderContext* scene, RenderFrame* rf, VertexData* v1, VertexData* v2, VertexData* v3, u16 colour) {
    b32 doClip;

    if (!IsPointVisible(v1->sVec.x, v1->sVec.y, scene->width, scene->height)) {
        if (!IsPointVisible(v2->sVec.x, v2->sVec.y, scene->width, scene->height) &&
            !IsPointVisible(v3->sVec.x, v3->sVec.y, scene->width, scene->height)) {

            u8 clipBits = 0;

            clipBits = UpdateClipBits(v1->sVec.x, clipBits, scene->width);
            clipBits = UpdateClipBits(v2->sVec.x, clipBits, scene->width);
            clipBits = UpdateClipBits(v3->sVec.x, clipBits, scene->width);

            if (sClipLookupTable[clipBits]) {
                return;
            }

            clipBits = 0;

            clipBits = UpdateClipBits(v1->sVec.y, clipBits, scene->height);
            clipBits = UpdateClipBits(v2->sVec.y, clipBits, scene->height);
            clipBits = UpdateClipBits(v3->sVec.y, clipBits, scene->height);

            if (sClipLookupTable[clipBits]) {
                return;
            }
        }
        doClip = TRUE;
    } else {
        doClip = (!IsPointVisible(v2->sVec.x, v2->sVec.y, scene->width, scene->height) ||
                  !IsPointVisible(v3->sVec.x, v3->sVec.y, scene->width, scene->height));
    }

    if (doClip) {
        if (!scene->currentBatchId) {
            i32 z = CalcZVec3i32(rf, v1->vVec);
            AddDrawNode(scene->zTree, z, DRAW_FUNC_SPANS_START);
        } else {
            BatchSpanStart(scene->zTree);
        }

        while (v1->vVec[2] < ZCLIPNEAR) {
            VertexData* t = v1;
            v1 = v2;
            v2 = v3;
            v3 = t;
        }

        DrawParamsLine*  spanDraw = BatchSpanLine(scene->zTree);
        if (v2->vVec[2] < ZCLIPNEAR) {
            spanDraw->x1 = v1->sVec.x;
            spanDraw->y1 = v1->sVec.y;

            Vec2i16 pos = ClipLineZ(v1, v2, scene->width, scene->height);
            spanDraw->x2 = pos.x;
            spanDraw->y2 = pos.y;

            DrawParamsPoint* spanNextPt = BatchSpanLineCont(scene->zTree);
            pos = ClipLineZ(v1, v3, scene->width, scene->height);
            spanNextPt->x = pos.x;
            spanNextPt->y = pos.y;
        } else {
            spanDraw->x1 = v1->sVec.x;
            spanDraw->y1 = v1->sVec.y;
            spanDraw->x2 = v2->sVec.x;
            spanDraw->y2 = v2->sVec.y;

            if (v3->vVec[2] < ZCLIPNEAR) {
                DrawParamsPoint* spanNextPt = BatchSpanLineCont(scene->zTree);
                Vec2i16 pos = ClipLineZ(v2, v3, scene->width, scene->height);
                spanNextPt->x = pos.x;
                spanNextPt->y = pos.y;

                spanNextPt = BatchSpanLineCont(scene->zTree);
                pos = ClipLineZ(v1, v3, scene->width, scene->height);
                spanNextPt->x = pos.x;
                spanNextPt->y = pos.y;
            } else {
                DrawParamsPoint* spanNextPt = BatchSpanLineCont(scene->zTree);
                spanNextPt->x = v3->sVec.x;
                spanNextPt->y = v3->sVec.y;
            }
        }
        DrawParamsPoint* spanNextPt = BatchSpanLineCont(scene->zTree);
        spanNextPt->x = v1->sVec.x;
        spanNextPt->y = v1->sVec.y;

        DrawParamsColour* spanColour = BatchSpanEnd(scene->zTree);
        spanColour->colour = Palette_GetIndexFor12bitColour(scene->palette, colour);
    } else {
        DrawParamsTri *tri = 0;
        if (scene->currentBatchId) {
            tri = BatchTri(scene->zTree);
        } else {
            i32 z = CalcZVec3i32(rf, v1->vVec);
            tri = AddTriNode(scene->zTree, z);
        }
        tri->points[0] = v1->sVec;
        tri->points[1] = v3->sVec;
        tri->points[2] = v2->sVec;
        tri->colour = Palette_GetIndexFor12bitColour(scene->palette, colour);
    }
}

MINTERNAL void RenderTriParams(RenderContext* renderContext, RenderFrame* rf, u16 param0, u16 param1, u16 param2) {
    u16 colourParam = param0 >> 4;
    u16 colour = CalcNormalColour(rf, colourParam, param2 & 0xff);
    if (colour & 0x8000) {
        return;
    }

    i16 vi1 = lo8s(param1);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);

    i16 vi2 = hi8s(param2);
    VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

    i16 vi3 = hi8s(param1);
    VertexData* v3 = ProjectVertex(renderContext, rf, vi3);

    RenderTriVerts(renderContext, rf, v1, v3, v2, colour);
}

MINTERNAL void RenderQuadVerts(RenderContext* renderContext, RenderFrame* rf, VertexData* v1, VertexData* v2,
                               VertexData* v4, VertexData* v3, u16 colour) {
    b32 doClip;

    if (!IsPointVisible(v1->sVec.x, v1->sVec.y, renderContext->width, renderContext->height)) {
        if (!IsPointVisible(v2->sVec.x, v2->sVec.y, renderContext->width, renderContext->height) &&
            !IsPointVisible(v4->sVec.x, v4->sVec.y, renderContext->width, renderContext->height) &&
            !IsPointVisible(v3->sVec.x, v3->sVec.y, renderContext->width, renderContext->height)) {

            u8 clipBits = 0;

            clipBits = UpdateClipBits(v1->sVec.x, clipBits, renderContext->width);
            clipBits = UpdateClipBits(v2->sVec.x, clipBits, renderContext->width);
            clipBits = UpdateClipBits(v3->sVec.x, clipBits, renderContext->width);
            clipBits = UpdateClipBits(v4->sVec.x, clipBits, renderContext->width);

            if (sClipLookupTable[clipBits]) {
                return;
            }

            clipBits = 0;

            clipBits = UpdateClipBits(v1->sVec.y, clipBits, renderContext->height);
            clipBits = UpdateClipBits(v2->sVec.y, clipBits, renderContext->height);
            clipBits = UpdateClipBits(v3->sVec.y, clipBits, renderContext->height);
            clipBits = UpdateClipBits(v4->sVec.y, clipBits, renderContext->height);

            if (sClipLookupTable[clipBits]) {
                return;
            }
        }
        doClip = TRUE;
    } else {
        doClip = (!IsPointVisible(v2->sVec.x, v2->sVec.y, renderContext->width, renderContext->height) ||
                  !IsPointVisible(v4->sVec.x, v4->sVec.y, renderContext->width, renderContext->height) ||
                  !IsPointVisible(v3->sVec.x, v3->sVec.y, renderContext->width, renderContext->height));
    }

    if (doClip) {
        if (!renderContext->currentBatchId) {
            i32 z = CalcZVec3i32(rf, v1->vVec);
            AddDrawNode(renderContext->zTree, z, DRAW_FUNC_SPANS_START);
        } else {
            BatchSpanStart(renderContext->zTree);
        }

        while (v1->vVec[2] < ZCLIPNEAR) {
            VertexData* t = v1;
            v1 = v2;
            v2 = v4;
            v4 = v3;
            v3 = t;
        }

        DrawParamsLine*  spanDraw = BatchSpanLine(renderContext->zTree);
        if (v2->vVec[2] < ZCLIPNEAR) {
            spanDraw->x1 = v1->sVec.x;
            spanDraw->y1 = v1->sVec.y;

            Vec2i16 pos = ClipLineZ(v1, v2, renderContext->width, renderContext->height);
            spanDraw->x2 = pos.x;
            spanDraw->y2 = pos.y;

            if (v3->vVec[2] >= ZCLIPNEAR) {
                pos = ClipLineZ(v3, v2, renderContext->width, renderContext->height);
                DrawParamsPoint* spanNextPt = BatchSpanLineCont(renderContext->zTree);
                spanNextPt->x = pos.x;
                spanNextPt->y = pos.y;

                spanNextPt = BatchSpanLineCont(renderContext->zTree);
                spanNextPt->x = v3->sVec.x;
                spanNextPt->y = v3->sVec.y;

                if (v4->vVec[2] < ZCLIPNEAR) {
                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    pos = ClipLineZ(v3, v4, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;

                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    pos = ClipLineZ(v1, v4, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;
                } else {
                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    spanNextPt->x = v4->sVec.x;
                    spanNextPt->y = v4->sVec.y;
                }
            } else {
                if (v4->vVec[2] >= ZCLIPNEAR) {
                    DrawParamsPoint* spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    pos = ClipLineZ(v4, v3, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;

                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    spanNextPt->x = v4->sVec.x;
                    spanNextPt->y = v4->sVec.y;
                } else {
                    DrawParamsPoint* spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    pos = ClipLineZ(v1, v4, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;
                }
            }
        } else {
            spanDraw->x1 = v1->sVec.x;
            spanDraw->y1 = v1->sVec.y;
            spanDraw->x2 = v2->sVec.x;
            spanDraw->y2 = v2->sVec.y;

            if (v3->vVec[2] < ZCLIPNEAR) {
                Vec2i16 pos = ClipLineZ(v3, v2, renderContext->width, renderContext->height);
                DrawParamsPoint* spanNextPt = BatchSpanLineCont(renderContext->zTree);
                spanNextPt->x = pos.x;
                spanNextPt->y = pos.y;

                if (v4->vVec[2] >= ZCLIPNEAR) {
                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    pos = ClipLineZ(v4, v3, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;

                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    spanNextPt->x = v4->sVec.x;
                    spanNextPt->y = v4->sVec.y;
                } else {
                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    pos = ClipLineZ(v1, v4, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;
                }
            } else {
                DrawParamsPoint* spanNextPt = BatchSpanLineCont(renderContext->zTree);
                spanNextPt->x = v3->sVec.x;
                spanNextPt->y = v3->sVec.y;

                if (v4->vVec[2] < ZCLIPNEAR) {
                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    Vec2i16 pos = ClipLineZ(v3, v4, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;

                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    pos = ClipLineZ(v1, v4, renderContext->width, renderContext->height);
                    spanNextPt->x = pos.x;
                    spanNextPt->y = pos.y;
                } else {
                    spanNextPt = BatchSpanLineCont(renderContext->zTree);
                    spanNextPt->x = v4->sVec.x;
                    spanNextPt->y = v4->sVec.y;
                }
            }
        }
        DrawParamsPoint* spanNextPt = BatchSpanLineCont(renderContext->zTree);
        spanNextPt->x = v1->sVec.x;
        spanNextPt->y = v1->sVec.y;

        DrawParamsColour* spanColour = BatchSpanEnd(renderContext->zTree);
        spanColour->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
    } else {
        DrawParamsQuad *quad = 0;
        if (renderContext->currentBatchId) {
            quad = BatchQuad(renderContext->zTree);
        } else {
            i32 z = CalcZVec3i32(rf, v1->vVec);
            quad = AddQuadNode(renderContext->zTree, z);
        }
        quad->points[0] = v1->sVec;
        quad->points[1] = v2->sVec;
        quad->points[2] = v3->sVec;
        quad->points[3] = v4->sVec;
        quad->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
    }
}

u32 Render_LoadFormattedString(SceneSetup* sceneSetup, u16 index, i8* outputBuffer, u32 outputBufferLen);

u32 Render_ProcessString(SceneSetup* sceneSetup, const i8* text, i8* outputBuffer, u32 outputBufferLen) {
    u8  c;
    u32 i = 0;
    u32 d = 0;
    do {
        c = text[i];
        if (c == 0xfd) {
            outputBuffer[d++] = 0x1e;
            outputBuffer[d++] = text[i + 1];
            i += 2;
        } else if (c == 0xfe) {
            outputBuffer[d++] = 0x1f;
            outputBuffer[d++] = text[i+2];
            outputBuffer[d++] = text[i+1];
            i += 3;
        } else if (c == 0xff) {
            u16 offset = (text[i+2] << 8) + text[i+1];
            d += Render_LoadFormattedString(sceneSetup, offset, outputBuffer + d, outputBufferLen);
            i += 3;
        } else {
            outputBuffer[d] = c;
            d++;
            i++;
        }
    } while (c != 0);
    return d;
}

enum LoadStringEnum {
    LoadString_ModelText = 22,
    LoadString_ModelTextWordWrap = 34
};

MINTERNAL i16 DrawBitmapChar(u8* bitmapFontData, u8* pixels, i16 x, i16 y, u8 charIndex, u8 colour) {
    if (x < 0 || x >= (SURFACE_WIDTH - FONT_MIN_WIDTH)) {
        return 0;
    }

    u32 charDataOffset = ((u32)(charIndex) * (FONT_HEIGHT + 1));
    bitmapFontData += charDataOffset;

    u32 drawOffset = (x * FONT_SCALE) + (y * FONT_SCALE * SURFACE_WIDTH);
    pixels += drawOffset;

    i32 i = 0;
    for (; i < FONT_HEIGHT; i++) {
        u8 bits = bitmapFontData[i];
        u8* pixelsStartLine = pixels;
        while (bits) {
            if (bits & 0x80) {
                *pixels = colour;
#if FONT_SCALE == 2
                *(pixels + 1) = colour;
                *(pixels + SURFACE_WIDTH) = colour;
                *(pixels + SURFACE_WIDTH + 1) = colour;
#elif FONT_SCALE == 4
                *(pixels + 1) = colour;
                *(pixels + 2) = colour;
                *(pixels + 3) = colour;
                *(pixels + SURFACE_WIDTH) = colour;
                *(pixels + SURFACE_WIDTH + 1) = colour;
                *(pixels + SURFACE_WIDTH + 2) = colour;
                *(pixels + SURFACE_WIDTH + 3) = colour;
                *(pixels + (SURFACE_WIDTH*2)) = colour;
                *(pixels + (SURFACE_WIDTH*2) + 1) = colour;
                *(pixels + (SURFACE_WIDTH*2) + 2) = colour;
                *(pixels + (SURFACE_WIDTH*2) + 3) = colour;
                *(pixels + (SURFACE_WIDTH*3)) = colour;
                *(pixels + (SURFACE_WIDTH*3) + 1) = colour;
                *(pixels + (SURFACE_WIDTH*3) + 2) = colour;
                *(pixels + (SURFACE_WIDTH*3) + 3) = colour;
#endif
            }
            pixels += FONT_SCALE;
            bits <<= 1;
        }
        pixels = pixelsStartLine + (FONT_SCALE * SURFACE_WIDTH);
    }

    return bitmapFontData[FONT_HEIGHT];
}

void Render_DrawBitmapText(SceneSetup* sceneSetup, const i8* text, Vec2i16 pos, u8 colour, b32 drawShadow) {
    if ((pos.y < 0) || (pos.y > (SURFACE_HEIGHT - FONT_HEIGHT))) {
        return;
    }

    u8* pixels = sceneSetup->raster->surface->pixels;
    u8* bitmapFontData = sceneSetup->assets.bitmapFontData;

    i16 initialX = pos.x;

    u8 c = text[0];
    u32 i = 1;
    while (c) {
        c -= 0x20;
        if (c & 0x80) {
            if (c == 0xfe) {
                u8 x = text[i++];
                pos.x = x * 2;
            } else if (c == 0xff) {
                u8 y = text[i++];
                u8 x = text[i++];
                pos.x = x * 2;
                pos.y = y;
            } else if (c == 0xed) {
                pos.y += FONT_NEW_LINE;
                pos.x = initialX;
            } else if (c == 0xe1) {
                u8 colIndex = text[i++];
                if (colIndex < sizeof(sceneSetup->bitmapFontColours)) {
                    colour = sceneSetup->bitmapFontColours[colIndex];
                }
            }
        } else {
            if (pos.x >= 0 && pos.x < (SURFACE_WIDTH - FONT_MIN_WIDTH)) {
                if (drawShadow) {
                    DrawBitmapChar(bitmapFontData, pixels, pos.x + 1, pos.y + 1, c,0);
                }
                pos.x += DrawBitmapChar(bitmapFontData, pixels, pos.x, pos.y, c, colour);
            }
        }
        c = text[i++];
    }
}

u32 WordWrap(i8* text, u32 textBufferLen, u32 len, u16 max) {
    u32 charsOnLine = 0;
    u32 lastWordEnd = 0;

    for (u32 i = 0; i < len; i++) {
        u8 c = text[i];
        charsOnLine++;
        if (c == 0x20) {
            lastWordEnd = i;
            if (charsOnLine > max) {
                text[i] = 0xd;
                charsOnLine = 0;
            }
        } else {

        }
    }
    return len;
}

u32 Render_LoadFormattedString(SceneSetup* sceneSetup, u16 index, i8* outputBuffer, u32 outputBufferLen) {
    if (index & 0x8000) {
        MLogf("Unhandled LoadFormattedString %d", index);
        outputBuffer[0] = 'N';
        outputBuffer[1] = '/';
        outputBuffer[2] = 'A';
        outputBuffer[3] = 0;
    } else if (index & 0x4000) {
        // Load string from current module
        index &= 0x7f;
        i8* text = (i8*)(sceneSetup->moduleStrings[index]);
        return Render_ProcessString(sceneSetup, text, outputBuffer, outputBufferLen);
    } else if (index >= 0x3000) {
        index = (index & 0x7f);
        switch (index) {
            case LoadString_ModelText: {
                return Render_ProcessString(sceneSetup, sceneSetup->modelText, outputBuffer, outputBufferLen);
            }
            case LoadString_ModelTextWordWrap: {
                u32 len = Render_ProcessString(sceneSetup, sceneSetup->modelText, outputBuffer, outputBufferLen);
                return WordWrap(outputBuffer, outputBufferLen, len, 40);
            }
            default:
                outputBuffer[0] = '?';
                outputBuffer[1] = '?';
                outputBuffer[2] = '?';
                outputBuffer[3] = 0;
                break;
        }
    } else {
        // Load fixed string exec?
        index &= 0x7f;
        MLogf("Unhandled LoadFormattedString %d", index);
        outputBuffer[0] = 'N';
        outputBuffer[1] = '/';
        outputBuffer[2] = 'A';
        outputBuffer[3] = 0;
    }
    return 0;
}

MINTERNAL i32 AddSpanLineCont(RenderContext* renderContext, RenderFrame* rf, i16 vi) {
    VertexData* v2  = rf->vertexTrans + vi;

    i16 x = v2->sVec.x;
    i16 y = v2->sVec.y;

    if (rf->complexCurrentlyZClipped == 0) {
        if (v2->vVec[2] < ZCLIPNEAR) {
            if (rf->complexSkipIfPointZClipped) {
                rf->complexLastVertexIx = 0;
                SkipRemainingSpanFuncs(renderContext, rf);
                return -1;
            }

            VertexData* v1 = rf->vertexTrans + rf->complexLastVertexIx;
            Vec2i16 v2Clip = ClipLineZ(v1, v2, renderContext->width, renderContext->height);
            x = v2Clip.x;
            y = v2Clip.y;
            rf->complexCurrentlyZClipped = 1;
        }

        DrawParamsPoint* drawParams = BatchSpanLineCont(renderContext->zTree);
        drawParams->x = x;
        drawParams->y = y;
    } else if (v2->vVec[2] > ZCLIPNEAR) {
        VertexData* v1 = rf->vertexTrans + rf->complexLastVertexIx;

        rf->complexCurrentlyZClipped = 0;

        Vec2i16 v1Clip = ClipLineZ(v2, v1, renderContext->width, renderContext->height);

        if (rf->complexJoinToBeginning == 0) {
            rf->complexJoinToBeginning = 1;
            rf->complexLastZClipPt = v1Clip;
            DrawParamsLine *drawParams = BatchSpanLine(renderContext->zTree);
            drawParams->x1 = v1Clip.x;
            drawParams->y1 = v1Clip.y;
            drawParams->x2 = v2->sVec.x;
            drawParams->y2 = v2->sVec.y;
            return 0;
        } else {
            DrawParamsPoint *drawParams = BatchSpanLineCont(renderContext->zTree);
            drawParams->x = v1Clip.x;
            drawParams->y = v1Clip.y;

            drawParams = BatchSpanLineCont(renderContext->zTree);
            drawParams->x = x;
            drawParams->y = y;
        }
    }

    rf->complexLastVertexIx = vi;
    return 0;
}

MINTERNAL i32 RComplexFinish(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    if (rf->complexJoinToBeginning) {
        i16 vi = rf->complexBeginVertexIx;

        AddSpanLineCont(renderContext, rf, vi);
        if (rf->complexCurrentlyZClipped) {
            DrawParamsPoint* drawParams = BatchSpanLineCont(renderContext->zTree);
            drawParams->x = rf->complexLastZClipPt.x;
            drawParams->y = rf->complexLastZClipPt.y;
        }
    }

    DrawParamsColour* drawParams = BatchSpanEnd(renderContext->zTree);
    drawParams->colour = Palette_GetIndexFor12bitColour(renderContext->palette, rf->complexColour);

    return -1;
}

MINTERNAL i32 RComplexBezierDraw(RenderContext* renderContext, Vec3i32 v1, Vec3i32 v2, Vec3i32 v3, Vec3i32 v4) {
    if (v1[2] < ZCLIPNEAR || v2[2] < ZCLIPNEAR || v3[2] < ZCLIPNEAR || v4[2] < ZCLIPNEAR) {
        return 0;
    } else {
        DrawParamsBezier* drawParams = BatchSpanBezier(renderContext->zTree);

        *(drawParams->pts) = ZProjectPoint(v1);
        *(drawParams->pts + 1) = ZProjectPoint(v2);
        *(drawParams->pts + 2) = ZProjectPoint(v3);
        *(drawParams->pts + 3) = ZProjectPoint(v4);

        return 1;
    }
}

MINTERNAL i32 RComplexBezier(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);

    i16 vi1 = hi8s(param1);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);
    rf->complexBeginVertexIx = vi1;

    i16 vi2 = lo8s(param1);
    VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

    i16 vi3 = hi8s(param2);
    VertexData* v3 = ProjectVertex(renderContext, rf, vi3);

    i16 vi4 = lo8s(param2);
    VertexData* v4 = ProjectVertex(renderContext, rf, vi4);

    if (v4->vVec[2] < ZCLIPNEAR || v1->vVec[2] < ZCLIPNEAR || v3->vVec[2] < ZCLIPNEAR || v2->vVec[2] < ZCLIPNEAR) {

        // Original code just renders a line instead, but that makes eagle look ugly in the intro, where it clips
        // intro the screen and causes a green triangle to fill half the screen.  Here we split the bezier in half
        // geometrically.
        Vec3i32 p0, p1, p2;
        Vec3132Midpoint(v1->vVec, v2->vVec, p0);
        Vec3132Midpoint(v2->vVec, v3->vVec, p1);
        Vec3132Midpoint(v3->vVec, v4->vVec, p2);

        Vec3i32 p3, p4, p5;
        Vec3132Midpoint(p0, p1, p3);
        Vec3132Midpoint(p1, p2, p4);
        Vec3132Midpoint(p3, p4, p5);

        if (!RComplexBezierDraw(renderContext, v1->vVec, p0, p3, p5)) {
            if (p5[2] < ZCLIPNEAR) {
                if (rf->complexSkipIfPointZClipped) {
                    SkipRemainingSpanFuncs(renderContext, rf);
                    return -1;
                } else {
                    if (v1->vVec[2] < ZCLIPNEAR) {
                        rf->complexJoinToBeginning = 0;
                        rf->complexCurrentlyZClipped = 1;
                    } else {
                        Vec2i16 p5Clip = ClipLineZVec3i32(v1->vVec, p5, renderContext->width, renderContext->height);
                        rf->complexLastZClipPt = p5Clip;
                        DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
                        drawLine->x1 = p5Clip.x;
                        drawLine->y1 = p5Clip.y;
                        drawLine->x2 = v1->sVec.x;
                        drawLine->y2 = v1->sVec.y;
                    }
                }
            } else if (v1->vVec[2] < ZCLIPNEAR) {
                Vec2i16 v1Clip = ClipLineZVec3i32(p5, v1->vVec, renderContext->width, renderContext->height);
                rf->complexCurrentlyZClipped = 1;
                DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
                Vec2i16 pts = ZProjectPoint(p5);
                rf->complexLastZClipPt = pts;
                drawLine->x1 = pts.x;
                drawLine->y1 = pts.y;
                drawLine->x2 = v1Clip.x;
                drawLine->y2 = v1Clip.y;
            } else {
                rf->complexCurrentlyZClipped = 0;
                DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
                Vec2i16 pts = ZProjectPoint(p5);
                drawLine->x1 = pts.x;
                drawLine->y1 = pts.y;
                drawLine->x2 = v1->sVec.x;
                drawLine->y2 = v1->sVec.y;
            }
        }

        if (!RComplexBezierDraw(renderContext, p5, p4, p2, v4->vVec)) {
            rf->complexLastVertexIx = vi4;
            rf->complexJoinToBeginning = 1;

            // Unable to clip bezier with z plane, just draw a line
            if (v4->vVec[2] < ZCLIPNEAR) {
                if (rf->complexSkipIfPointZClipped) {
                    SkipRemainingSpanFuncs(renderContext, rf);
                    return -1;
                } else {
                    if (p5[2] < ZCLIPNEAR) {
                        rf->complexJoinToBeginning = 0;
                        rf->complexCurrentlyZClipped = 1;
                    } else {
                        Vec2i16 v4Clip = ClipLineZVec3i32(p5, v4->vVec, renderContext->width, renderContext->height);
                        rf->complexLastZClipPt = v4Clip;
                        Vec2i16 pts = ZProjectPoint(p5);
                        DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
                        drawLine->x1 = v4Clip.x;
                        drawLine->y1 = v4Clip.y;
                        drawLine->x2 = pts.x;
                        drawLine->y2 = pts.y;
                    }
                }
            } else if (p5[2] < ZCLIPNEAR) {
                Vec2i16 p5Clip = ClipLineZVec3i32(v4->vVec, p5, renderContext->width, renderContext->height);
                rf->complexJoinToBeginning = 0;
                rf->complexCurrentlyZClipped = 1;
                DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
                drawLine->x1 = v4->sVec.x;
                drawLine->y1 = v4->sVec.y;
                drawLine->x2 = p5Clip.x;
                drawLine->y2 = p5Clip.y;
            } else {
                rf->complexCurrentlyZClipped = 0;
                Vec2i16 pts = ZProjectPoint(p5);
                DrawParamsLine* drawLine = BatchSpanLine(renderContext->zTree);
                drawLine->x1 = v4->sVec.x;
                drawLine->y1 = v4->sVec.y;
                drawLine->x2 = pts.x;
                drawLine->y2 = pts.y;
            }
        } else {
            rf->complexLastVertexIx = vi4;
            rf->complexCurrentlyZClipped = 0;
            rf->complexJoinToBeginning = 1;
        }
    } else {
        rf->complexLastVertexIx = vi4;
        rf->complexCurrentlyZClipped = 0;
        rf->complexJoinToBeginning = 1;

        DrawParamsBezier* drawParams = BatchSpanBezier(renderContext->zTree);
        drawParams->pts[0] = v1->sVec;
        drawParams->pts[1] = v2->sVec;
        drawParams->pts[2] = v3->sVec;
        drawParams->pts[3] = v4->sVec;
    }
    return 0;
}

MINTERNAL i32 RComplexFuncLine(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param1 = ByteCodeRead16u(rf);

    i16 vi1 = lo8s(funcParam);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);
    rf->complexBeginVertexIx = vi1;

    i16 vi2 = lo8s(param1);
    VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

    return ClipSpanLine(renderContext, v1, v2, vi2);
}

MINTERNAL i32 RComplexLineCont(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    i16 vi = lo8s(funcParam);
    VertexData* v = ProjectVertex(renderContext, rf, vi);

    return AddSpanLineCont(renderContext, rf, vi);
}

MINTERNAL i32 RComplexBezierCont(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    i16 vi1 = rf->complexLastVertexIx;
    VertexData* v1 = rf->vertexTrans + vi1;

    u16 param1 = ByteCodeRead16u(rf);

    if (v1->vVec[2] >= ZCLIPNEAR) {
        i16 vi2 = lo8s(funcParam);
        VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

        i16 vi3 = hi8s(param1);
        VertexData* v3 = ProjectVertex(renderContext, rf, vi3);

        i16 vi4 = lo8s(param1);
        VertexData* v4 = ProjectVertex(renderContext, rf, vi4);

        if (v4->vVec[2] < ZCLIPNEAR || v1->vVec[2] < ZCLIPNEAR || v3->vVec[2] < ZCLIPNEAR || v2->vVec[2] < ZCLIPNEAR) {
            return ClipSpanLine(renderContext, v1, v4, vi4);
        } else {
            rf->complexLastVertexIx = vi4;
            rf->complexCurrentlyZClipped = 0;
            rf->complexJoinToBeginning = 1;

            DrawParamsBezier* drawParams = BatchSpanBezier(renderContext->zTree);
            drawParams->pts[0] = v1->sVec;
            drawParams->pts[1] = v2->sVec;
            drawParams->pts[2] = v3->sVec;
            drawParams->pts[3] = v4->sVec;
            rf->complexLastVertexIx = vi4;
        }
    } else {
        i16 vi4 = lo8s(param1);
        VertexData* v4 = ProjectVertex(renderContext, rf, vi4);

        if (v4->vVec[2] >= ZCLIPNEAR) {
            rf->complexCurrentlyZClipped = 0;

            Vec2i16 v1Clip = ClipLineZ(v4, v1, renderContext->width, renderContext->height);

            if (rf->complexJoinToBeginning == 0) {
                rf->complexJoinToBeginning = 1;
                rf->complexLastZClipPt = v1Clip;
                DrawParamsLine *drawParams = BatchSpanLine(renderContext->zTree);
                drawParams->x1 = v1Clip.x;
                drawParams->y1 = v1Clip.y;
                drawParams->x2 = v4->sVec.x;
                drawParams->y2 = v4->sVec.y;
            } else {
                DrawParamsPoint *drawParams = BatchSpanLineCont(renderContext->zTree);
                drawParams->x = v1Clip.x;
                drawParams->y = v1Clip.y;

                drawParams = BatchSpanLineCont(renderContext->zTree);
                drawParams->x = v4->sVec.x;
                drawParams->y = v4->sVec.y;
            }
        }
        rf->complexLastVertexIx = vi4;
    }

    return 0;
}

MINTERNAL i32 RComplexJoin(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    if (!rf->complexJoinToBeginning) {
        return 0;
    }

    if (AddSpanLineCont(renderContext, rf, rf->complexBeginVertexIx)) {
        return -1;
    }

    if (rf->complexCurrentlyZClipped) {
        DrawParamsPoint* drawParams = BatchSpanLineCont(renderContext->zTree);
        drawParams->x = rf->complexLastZClipPt.x;
        drawParams->y = rf->complexLastZClipPt.y;
    }

    return 0;
}

//
// Perspective projected points for a cubic Bezier approximation of a circle.
//
// A circle when perspective projected results in conic section on the view plane - usually a ellipse.
//
// Only ellipses are handled here.
//
// Cubic Beziers have 2 control points, and with two of them a close approximation of a circle/ellipse
// can be generated.
//
// Points are generated for two cubic Bezier curves:
//
// 5                    2
//           |
//           |
//           |                 Curve2 - facing away from viewer in case of cones, so not always drawn
//           |
//           |
// 3 ------------------ 0
//         a |   axis1
//         x |
//         i |                 Curve1 - facing viewer in case of cones
//         s |
//         2 |
// 4                    1
//
// The two cubic Bezier curves would be 3,4,1,0 and 3,5,2,0
//
// 0 and 3 are points on the projected ellipse, the others are control points, as such axis2 is not the
// same length as the second axis of the ellipse, but, rather, a bit larger to accommodate the control points.
MINTERNAL int ProjectCircleBezierPoints(RenderContext* renderContext, VertexData* v, Vec3i32 axis1, Vec3i32 axis2, Vec2i16* ptsOut) {
    Vec3i32 p1;
    Vec3i32 p2;

    Vec3i32Add(v->vVec, axis1, p1);
    if (p1[2] < ZCLIPNEAR) {
        return 0;
    }
    *(ptsOut) = ZProjectPoint(p1);

    Vec3i32Add(p1, axis2, p2);
    if (p2[2] < ZCLIPNEAR) {
        return 0;
    }
    *(ptsOut + 1) = ZProjectPoint(p2);

    Vec3i32Sub(p1, axis2, p2);
    if (p2[2] < ZCLIPNEAR) {
        return 0;
    }
    *(ptsOut + 2) = ZProjectPoint(p2);

    Vec3i32Sub(v->vVec, axis1, p1);
    if (p1[2] < ZCLIPNEAR) {
        return 0;
    }
    *(ptsOut + 3) = ZProjectPoint(p1);

    Vec3i32Add(p1, axis2, p2);
    if (p2[2] < ZCLIPNEAR) {
        return 0;
    }
    *(ptsOut + 4) = ZProjectPoint(p2);

    Vec3i32Sub(p1, axis2, p2);
    if (p2[2] < ZCLIPNEAR) {
        return 0;
    }
    *(ptsOut + 5) = ZProjectPoint(p2);

    return 1;
}

// Draw ring
MINTERNAL int RComplexCircle(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    i16 vi = lo8s(funcParam);
    VertexData* v = ProjectVertex(renderContext, rf, vi);

    if (v->vVec[2] < ZCLIPNEAR) {
        rf->complexJoinToBeginning = 0;
        return 0;
    }

    u16 param1 = ByteCodeRead16u(rf);
    u8 normalIndex = param1 & 0x7f;
    u16 scale = (param1 >> 8) << rf->scale;

    if (normalIndex != rf->complexNormalIndex) {
        Vec3i32 n;
        GetNormalVec3i32(rf, normalIndex, n);

        MultVec3i32Matrix(rf->modelViewMatrix, n, rf->complexNormal);

        rf->complexNormalIndex = normalIndex;
    }

    i32 dx = rf->complexNormal[0] * rf->complexNormal[0];
    i32 dy = rf->complexNormal[1] * rf->complexNormal[1];
    i32 lenSqrd = dx + dy;

    Vec3i32 axis1;
    Vec3i32 axis2;

    if (lenSqrd < (200 * 200)) {
        axis1[0] = scale;
        axis1[1] = 0;
        axis1[2] = 0;

        axis2[0] = 0;
        axis2[1] = (F16_MINUS_TWO_THIRDS * scale) >> 15;
        axis2[2] = 0;
    } else {
        i32 len = FMath_SqrtFunc32(lenSqrd);
        scale <<= 1;

        i32 y1 = rf->complexNormal[0] << 14;
        y1 /= len;
        y1 *= scale;
        y1 = y1 << 1;
        axis1[1] = y1 >> 16;

        i32 x1 = rf->complexNormal[1] << 14;
        x1 = ((x1 / len) * scale) << 1;
        axis1[0] = -(x1 >> 16);

        axis1[2] = 0;

        i32 radius = (scale * F16_TWO_THIRDS) >> 15;

        i32 x2 = ((rf->complexNormal[0] * rf->complexNormal[2]) / len) * radius;
        axis2[0] = x2 >> 15;

        i32 y2 = ((rf->complexNormal[1] * rf->complexNormal[2]) / len) * radius;
        axis2[1] = y2 >> 15;
        axis2[2] = (-len * radius) >> 15;
    }

    Vec2i16 pts[6];
    if (!ProjectCircleBezierPoints(renderContext, v, axis1, axis2, pts)) {
        return 0;
    }

    DrawParamsBezier* b1 = BatchSpanBezier(renderContext->zTree);
    b1->pts[0] = pts[3];
    b1->pts[1] = pts[5];
    b1->pts[2] = pts[2];
    b1->pts[3] = pts[0];

    DrawParamsBezier* b2 = BatchSpanBezier(renderContext->zTree);
    b2->pts[0] = pts[3];
    b2->pts[1] = pts[4];
    b2->pts[2] = pts[1];
    b2->pts[3] = pts[0];

    rf->complexJoinToBeginning = 0;

    return 0;
}

MINTERNAL void InterpretComplexByteCode(RenderContext* renderContext, RenderFrame* rf) {
    // Call render funcs until hit end byte code, or draw buffer is full
    while (!ByteCodeIsDone(rf)) {
#ifdef FINSPECTOR
        u32 byteCodeOffset = rf->byteCodePos - rf->fileDataStartAddress;
        if (renderContext->logLevel) {
            ByteCodeTrace trace;
            trace.index = byteCodeOffset;
            trace.result = 0;
            MArrayAdd(*(renderContext->byteCodeTrace), trace);
        }
        renderContext->zTree->insOffsetTmp = byteCodeOffset;
#endif
        int result = 0;
        u16 funcParam = ByteCodeRead16u(rf);
        u8 func = ((funcParam >> 9) & 0x7);
        switch (func) {
            case RComplexFunc_Done:
                result = RComplexFinish(renderContext, funcParam);
                break;
            case RComplexFunc_Bezier:
                result = RComplexBezier(renderContext, funcParam);
                break;
            case RComplexFunc_Line:
                result = RComplexFuncLine(renderContext, funcParam);
                break;
            case RComplexFunc_LineCont:
                result = RComplexLineCont(renderContext, funcParam);
                break;
            case RComplexFunc_BezierCont:
                result = RComplexBezierCont(renderContext, funcParam);
                break;
            case RComplexFunc_LineJoin:
                result = RComplexJoin(renderContext, funcParam);
                break;
            case RComplexFunc_Circle:
                result = RComplexCircle(renderContext, funcParam);
                break;
            default:
                MLogf("Undefined complex function");
                result = -1;
                break;
        }
        if (result < 0) {
            break;
        }
    }
}

MINTERNAL void RotateMatrix(Matrix3x3i16 matrix, u16 row1, u16 row2, i16 sine, i16 cosine) {
    i32 x1 = matrix[row1][0];
    i32 y1 = matrix[row1][1];
    i32 z1 = matrix[row1][2];

    i32 x2 = matrix[row2][0];
    i32 y2 = matrix[row2][1];
    i32 z2 = matrix[row2][2];

    i32 dx = (((x1 * cosine) + (x2 * sine)) >> (u16)15);
    i32 dy = (((y1 * cosine) + (y2 * sine)) >> (u16)15);
    i32 dz = (((z1 * cosine) + (z2 * sine)) >> (u16)15);

    matrix[row1][0] = (i16)dx;
    matrix[row1][1] = (i16)dy;
    matrix[row1][2] = (i16)dz;

    dx = (((z2 * cosine) - (z1 * sine)) >> (u16)15);
    dy = (((y2 * cosine) - (y1 * sine)) >> (u16)15);
    dz = (((x2 * cosine) - (x1 * sine)) >> (u16)15);

    matrix[row2][2] = (i16)dx;
    matrix[row2][1] = (i16)dy;
    matrix[row2][0] = (i16)dz;
}

MINTERNAL void RenderQuadParams(RenderContext* renderContext, RenderFrame* rf, u16 param0, u16 param1, u16 param2, u16 param3) {
    u16 colour = CalcNormalColour(rf, param0, param3 & 0xff);
    if (colour & 0x8000) {
        return;
    }

    i16 vi2 = lo8s(param1);
    VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

    i16 vi4 = hi8s(param2);
    VertexData* v4 = ProjectVertex(renderContext, rf, vi4);

    i16 vi3 = hi8s(param1);
    VertexData* v3 = ProjectVertex(renderContext, rf, vi3);

    i16 vi1 = lo8s(param2);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);

    RenderQuadVerts(renderContext, rf, v1, v2, v3, v4, colour);
}

MINTERNAL void AddBasicCircle(RenderContext* renderContext, RenderFrame* rf, VertexData* v, u16 diameter,
                              i16 screenX, i16 screenY, u16 colour) {

    DrawParamsCircle *drawParams = NULL;
    if (renderContext->currentBatchId) {
        drawParams = BatchCircle(renderContext->zTree);
    } else {
        i32 depth = CalcZVec3i32(rf, v->vVec);
        drawParams = AddCircleNode(renderContext->zTree, depth);
    }
    drawParams->x = screenX;
    drawParams->y = screenY;
    drawParams->diameter = diameter;
    drawParams->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
}

MINTERNAL void AddHighlightOrBacklitCircle(RenderContext* renderContext, RenderFrame* rf, VertexData* v, i16 diameter,
                                           i16 screenX, i16 screenY, u16 colour, b32 lightSource) {

    if (screenX < 0 || screenX > renderContext->width || screenY < 0 || screenY > renderContext->height) {
        return;
    }

    if (lightSource) {
        DrawParamsFlare *drawParams = NULL;
        if (renderContext->currentBatchId) {
            drawParams = BatchHighlight(renderContext->zTree);
        } else {
            i32 depth = CalcZVec3i32(rf, v->vVec);
            drawParams = AddHighlightNode(renderContext->zTree, depth);
        }
        drawParams->x = screenX;
        drawParams->y = screenY;
        drawParams->diameter = diameter;
        drawParams->innerColour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
        u16 colour2 = ((colour & 0xeee) >> 1) + 0x222;
        drawParams->outerColour = Palette_GetIndexFor12bitColour(renderContext->palette, colour2);
    } else {
        DrawParamsRingedCircle *drawParams = NULL;
        if (renderContext->currentBatchId) {
            drawParams = BatchRingedCircle(renderContext->zTree);
        } else {
            i32 depth = CalcZVec3i32(rf, v->vVec);
            drawParams = AddRingedCircleNode(renderContext->zTree, depth);
        }
        drawParams->x = screenX - 1;
        drawParams->y = screenY - 1;
        drawParams->diameter = diameter;
        drawParams->innerColour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
        u16 colour2 = ((colour & 0xeee) >> 1) + 0x222;
        drawParams->outerColour = Palette_GetIndexFor12bitColour(renderContext->palette, colour2);
    }
}

MINTERNAL void AddHighlight(RenderContext* renderContext, RenderFrame* rf, VertexData* v, i16 diameter,
                            i16 screenX, i16 screenY, u16 colour) {

    if (screenX < 0 || screenX > renderContext->width || screenY < 0 || screenY > renderContext->height) {
        return;
    }

    DrawParamsFlare *drawParams = NULL;
    if (renderContext->currentBatchId) {
        drawParams = BatchHighlight(renderContext->zTree);
    } else {
        i32 depth = CalcZVec3i32(rf, v->vVec);
        drawParams = AddHighlightNode(renderContext->zTree, depth);
    }
    drawParams->x = screenX;
    drawParams->y = screenY;
    drawParams->diameter = diameter;
    drawParams->innerColour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
    u16 colour2 = ((colour & 0xeee) >> 1) + 0x222;
    drawParams->outerColour = Palette_GetIndexFor12bitColour(renderContext->palette, colour2);
}

MINTERNAL void RenderCircleParams(RenderContext* renderContext, RenderFrame* rf, VertexData* v, i16 diameter, i32 radiusParam,
                                  i16 screenX, i16 screenY, u16 colourParam, u16 extraColour, b32 lightSource) {

    i32 radius = diameter >> 1;

    b32 isVisible = IsCircleVisible(screenX, screenY, radius, renderContext->width, renderContext->height);
    if (!isVisible) {
        return;
    }

    u16 colour = (colourParam & (u16) 0xeee);

    if (colourParam & 0x100) {
        // Not lit by global light source

        if (colourParam & 0x10) {
            colour += rf->baseColour;
        }

        screenX -= 1;
        screenY -= 1;

        if (diameter == 0) {
            // Too small to show outline of light source, but we still want to display flaring
            if (lightSource) {
                i32 z = v->vVec[2];
                z >>= 8;
                while (z > 0x8000) {
                    radiusParam >>= 1;
                    z >>= 1;
                }
                radiusParam <<= 8;
                i32 d;
                if (z == 0) {
                    d = radiusParam;
                } else {
                    d = radiusParam / z;
                }

                if (d) {
                    d >>= 3;
                    i16 diameter2 = sDrawHighlightIndex[d & 0x1f];

                    AddHighlight(renderContext, rf, v, diameter2, screenX, screenY, colour);
                } else {
                    AddBasicCircle(renderContext, rf, v, diameter, screenX, screenY, colour);
                }
            } else {
                AddBasicCircle(renderContext, rf, v, diameter, screenX, screenY, colour);
            }
        } else if (lightSource && diameter <= HIGHLIGHTS_BIG) {
            AddHighlight(renderContext, rf, v, diameter, screenX, screenY, colour);
        } else {
            AddBasicCircle(renderContext, rf, v, diameter, screenX, screenY, colour);
        }
    } else {
        // Lit by global light source
        if (colourParam & 0x10) {
            colour += rf->baseColour;
        }

        i16 lightZDir = rf->lightV[2];

        if (diameter <= HIGHLIGHTS_BIG) {
            if (lightZDir > -0x6fff) {
                colour += rf->shadeRamp[7];
            }
            AddHighlightOrBacklitCircle(renderContext, rf, v, diameter, screenX, screenY, colour, lightSource);
        } else {
            if (lightZDir < -0x7d00) {
                AddHighlightOrBacklitCircle(renderContext, rf, v, diameter, screenX, screenY, colour, lightSource);
            } else if (lightZDir > 0x7d00) {
                if (lightZDir > -0x6fff) {
                    colour += rf->shadeRamp[7];
                }
                AddHighlightOrBacklitCircle(renderContext, rf, v, diameter, screenX, screenY, colour, lightSource);
            } else {
                // Draw shaded ball, using the span render twice once for each colour
                if (renderContext->currentBatchId) {
                    BatchSpanStart(renderContext->zTree);
                } else {
                    i32 depth = CalcZVec3i32(rf, v->vVec);
                    AddDrawNode(renderContext->zTree, depth, DRAW_FUNC_SPANS_START);
                }

                DrawParamsBezier* bz1 = BatchSpanBezier(renderContext->zTree);
                DrawParamsBezier* bz2 = BatchSpanBezier(renderContext->zTree);

                DrawParamsColour* drawShaded = BatchSpanDraw(renderContext->zTree);
                drawShaded->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);

                DrawParamsBezier* bz3 = BatchSpanBezier(renderContext->zTree);
                DrawParamsBezier* bz4 = BatchSpanBezier(renderContext->zTree);

                DrawParamsColour *drawLit = BatchSpanEnd(renderContext->zTree);
                u16 shadedColour = colour + rf->shadeRamp[2];
                drawLit->colour = Palette_GetIndexFor12bitColour(renderContext->palette, shadedColour);

                i32 light2d2 = ((i32)rf->lightV[0] * (i32)rf->lightV[0]) + ((i32)rf->lightV[1] * (i32)rf->lightV[1]);
                i32 light2d = FMath_SqrtFunc32(light2d2);
                i32 lx = ((i32)rf->lightV[0]) << 15;
                i32 vx = lx;

                i16 d = 0;
                if (light2d != 0) {
                    vx /= light2d;
                } else {
                    vx = (rf->lightV[0] >> 15) ^ 0x7fff;
                }

                d = (vx * radius) >> 15;

                i16 shadeAxisY = -d;
                i16 ctrlPointXOffset = (d * F16_MINUS_TWO_THIRDS) >> 14;

                i32 vy = (-rf->lightV[1]) << 15;
                if (light2d != 0) {
                    vy /= light2d;
                } else {
                    vy = (rf->lightV[1] >> 15) ^ 0x7fff;
                }

                d = (vy * radius) >> 15;

                i16 shadeAxisX = d;
                i16 ctrlPointYOffset = (d * F16_MINUS_TWO_THIRDS) >> 14;

                Vec2i16 pt1 = {screenX + shadeAxisX, screenY + shadeAxisY};
                bz1->pts[0] = pt1;
                bz2->pts[3] = pt1;
                bz3->pts[0] = pt1;
                bz4->pts[3] = pt1;

                pt1.x -= ctrlPointXOffset;
                pt1.y -= ctrlPointYOffset;
                bz1->pts[1] = pt1;

                pt1.x += (ctrlPointXOffset << 1);
                pt1.y += (ctrlPointYOffset << 1);
                bz3->pts[1] = pt1;

                Vec2i16 pt2 = {screenX - shadeAxisX, screenY - shadeAxisY};

                bz1->pts[3] = pt2;
                bz2->pts[0] = pt2;
                bz3->pts[3] = pt2;
                bz4->pts[0] = pt2;

                pt2.x -= ctrlPointXOffset;
                pt2.y -= ctrlPointYOffset;
                bz1->pts[2] = pt2;

                pt2.x += (ctrlPointXOffset << 1);
                pt2.y += (ctrlPointYOffset << 1);
                bz3->pts[2] = pt2;

                i32 lz = -rf->lightV[2];
                ctrlPointXOffset = (lz * ctrlPointXOffset) >> 15;
                ctrlPointYOffset = (lz * ctrlPointYOffset) >> 15;

                pt1.x = bz1->pts[0].x + ctrlPointXOffset;
                pt1.y = bz1->pts[0].y + ctrlPointYOffset;

                bz2->pts[2] = pt1;
                bz4->pts[2] = pt1;

                pt2.x = bz1->pts[3].x + ctrlPointXOffset;
                pt2.y = bz1->pts[3].y + ctrlPointYOffset;

                bz2->pts[1] = pt2;
                bz4->pts[1] = pt2;
            }
        }
    }
}

#ifdef FINSPECTOR
MINTERNAL b32 AllowModelRender(RenderContext* renderContext, u16 modelIndex) {
    return !(renderContext->sceneSetup->hideModel[modelIndex]);
}
#endif

MINTERNAL int RenderSubModel(RenderContext* renderContext, RenderFrame* rf, u16 funcParam, u16 param1, i16 vi,
                             u16 scale, u16 baseColour) {

    u16 modelIndex = (funcParam >> 5) & 0xff;

    ModelData* modelData = Render_GetModel(rf->renderContext, modelIndex);

    scale = scale + modelData->scale1 + modelData->scale2 - rf->zScale;

    u32 radius = modelData->radius;
    radius <<= scale;

    VertexData* v = rf->vertexTrans + vi;
    i32 x = v->vVec[0];
    i32 y = v->vVec[1];
    i32 z = v->vVec[2];

    if (!IsInViewport(radius, x, y, z)) {
        if (param1 & 0x8000) {
            ByteCodeSkipBytes(rf, 4);
            return 0;
        } else {
            return 0;
        }
    }

#ifdef FINSPECTOR
    if (renderContext->logLevel) {
        MArrayAdd(*(renderContext->loadedModelIndexes), modelIndex);
    }
#endif

    i16 v1i = 0;
    i16 v2i = 0;
    i16 v3i = 0;
    i16 v4i = 0;

    RenderFrame* newRenderFrame;

    if (param1 & (u16)0x8000) {
        // Pass down parent frame vertices, these can be accessed with negative indices
        u16 param2 = ByteCodeRead16u(rf);
        u16 param3 = ByteCodeRead16u(rf);

        v1i = lo8s(param3);
        if (v1i == 0x7f) {
            goto doneProjects;
        }
        ProjectVertex(renderContext, rf, v1i);

        v2i = lo8s(param2);
        if (v2i == 0x7f) {
            goto doneProjects;
        }
        ProjectVertex(renderContext, rf, v2i);

        v3i = hi8s(param2);
        if (v3i == 0x7f) {
            goto doneProjects;
        }
        ProjectVertex(renderContext, rf, v3i);

        v4i = hi8s(param3);
        if (v4i == 0x7f) {
            goto doneProjects;
        }

        ProjectVertex(renderContext, rf, v4i);
    }

doneProjects:

    newRenderFrame = PushRenderFrame(renderContext);
    newRenderFrame->parentFrameVertexIndexes[0] = vi;
    newRenderFrame->parentFrameVertexIndexes[1] = v1i;
    newRenderFrame->parentFrameVertexIndexes[2] = v2i;
    newRenderFrame->parentFrameVertexIndexes[3] = v3i;
    newRenderFrame->parentFrameVertexIndexes[4] = v4i;

    CopyVertexView(v, newRenderFrame->objViewVec);
    newRenderFrame->zScale = rf->zScale;
    newRenderFrame->renderContext = rf->renderContext;
    newRenderFrame->parentVertexTrans = rf->vertexTrans;
    newRenderFrame->parentVertices = rf->vertexTrans;
    newRenderFrame->scale = scale;
    newRenderFrame->tmpVariable[0] = rf->tmpVariable[0] + 1;
    newRenderFrame->tmpVariable[1] = rf->tmpVariable[1];
    newRenderFrame->tmpVariable[2] = rf->tmpVariable[2];
    newRenderFrame->baseColour = baseColour;

    SetupNewTransformMatrix(renderContext, newRenderFrame, rf, param1 >> 8);

#ifdef FINSPECTOR
    if (AllowModelRender(renderContext, modelIndex)) {
#endif
        FrameRenderObjects(renderContext, modelData);
#ifdef FINSPECTOR
    }
#endif

    PopRenderFrame(renderContext);

    return 0;
}

MINTERNAL void InterpretModelCode(RenderContext* renderContext, RenderFrame *rf);

MINTERNAL int RenderCircle(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 colourParam = funcParam >> 4;
    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);

    u16 extraColour = 0;
    if ((colourParam & (u16)0xf00) == 0xe00) {
        extraColour = (colourParam >> 1) & (u16)0x7;
        extraColour += (colourParam >> 2) & (u16)0x38;
        colourParam = (u16)0x10 & colourParam; // Apply colour tint as normal
    }

    b32 lightSource = (param2 & 0x80);

    i16 vi1 = hi8s(param2);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);

    u32 radiusParam = GetValueForParam16(rf, param1);
    i32 radius = radiusParam >> 1;

    i16 scale = rf->scale - 7;
    if (scale >= 0) {
        scale &= 0x3f;
        if (scale) {
            radius <<= scale;
        }
    } else {
        scale = -scale;
        radius >>= scale;
    }

    i32 centerZ = v1->vVec[2];
    if (centerZ < ZCLIPNEAR) {
        return 0;
    }

    i32 nearestZ = (centerZ >> 3) - radius;

    if (nearestZ >= 0) {
        u32 radius1 = radius;
        while (centerZ > 0x8000) {
            radius1 >>= 1;
            centerZ >>= 1;
        }

        i16 diameter = (radius1 << ZSCALE) / centerZ;

        RenderCircleParams(renderContext, rf, v1, diameter, radius, v1->sVec.x, v1->sVec.y, colourParam, extraColour, lightSource);
    } else {
        u32 radius1 = radius >> 1;
        if (centerZ < radius1) {
            return 0;
        }

        u32 r = radius1;

        i32 x = v1->vVec[0];
        i32 y = v1->vVec[1];
        i32 z = v1->vVec[2];

        if (x < 0) {
            r |= -x;
        } else {
            r |= x;
        }

        if (y < 0) {
            r = -y | r;
        } else {
            r = y | r;
        }

        if (z < 0) {
            r = -z | r;
        } else {
            r = z | r;
        }

        if (r) {
            i16 c = -1;

            do {
                c++;
                r <<= 1;
            } while (!(r & 0x80000000));

            if (c > 0) {
                x <<= c;
                y <<= c;
                z <<= c;
                radius1 <<= c;
            }
        }

        i32 r16 = radius1 >> 16;
        i32 rSqrd = (r16 * r16);

        i32 x16 = x >> 16;
        i32 dx = (x16 * x16);

        i32 y16 = y >> 16;
        i32 dy = (y16 * y16);

        i32 z16 = z >> 16;
        i32 dz = (z16 * z16) - rSqrd;

        i32 depthSqrd = dz >> 16;
        if (depthSqrd == 0) {
            return 0;
        }

        u16 distRatio = (z >> (16 - ZSCALE)) / depthSqrd;

        i32 x2 = (x16 * distRatio) >> 16;
        i32 y2 = (y16 * distRatio) >> 16;

        i32 radiusRatio = (radius1 >> (15 - ZSCALE)) / depthSqrd;

        i32 distSqrd = (dz + dx + dy);
        i32 dist = FMath_SqrtFunc32(distSqrd);

        i32 diameter = (dist * radiusRatio) >> 16;
        i16 screenX = x2 + (renderContext->width / 2);
        i16 screenY = (renderContext->height / 2) - y2;

        RenderCircleParams(renderContext, rf, v1, diameter, radius, screenX, screenY, colourParam, extraColour, lightSource);
    }

    return 0;
}

// project line
MINTERNAL int RenderLine(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param0 = funcParam;
    u16 param1 = ByteCodeRead16u(rf);

    RenderLineParams(renderContext, rf, param0, param1);

    return 0;
}

// project triangle
MINTERNAL int RenderTri(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);

    RenderTriParams(renderContext, rf, funcParam, param1, param2);

    return 0;
}

// project quad
MINTERNAL int RenderQuad(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 colourParam = funcParam >> 4;
    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);
    u16 param3 = ByteCodeRead16u(rf);

    RenderQuadParams(renderContext, rf, colourParam, param1, param2, param3);

    return 0;
}

// Draw complex path on plane - typically vector text or complex polygons.
// Sometimes not on a single plane, in this case the rendering will have artifacts.
MINTERNAL int RenderComplex(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 colourParam = funcParam >> 4;
    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodePeek16u(rf);

    u8 normalIndex = param1 & 0x7f;
    u16 colour = CalcNormalColour(rf, colourParam, normalIndex);
    if (colour & 0x8000) {
        ByteCodeSkipBytes(rf, param1 >> 8);
        return 0;
    }

    rf->complexColour = colour;
    rf->complexNormalIndex = 0;
    rf->complexSkipIfPointZClipped = param1 & 0x80;

    i16 vi1 = lo8s(param2);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);

    if (renderContext->currentBatchId) {
        u16* drawFunc = BatchSpanStart(renderContext->zTree);
        renderContext->lastBatchFunc = (DrawFunc*)drawFunc;
    } else {
        i32 z = CalcZVec3i32(rf, v1->vVec);
        RasterOpNode *drawNode = AddDrawNode(renderContext->zTree, z, DRAW_FUNC_SPANS_START);
        renderContext->lastBatchFunc = &(drawNode->func);
    }

    renderContext->lastBatchOffset = renderContext->zTree->offset;

    InterpretComplexByteCode(renderContext, rf);

    return 0;
}

// Batch start and/or end
MINTERNAL int RenderBatch(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);
    u16 batchId = funcParam >> 5;

    if (batchId == 0) {
        if (renderContext->currentBatchId != 0) {
            WriteDrawFunc(renderContext->zTree, DRAW_FUNC_BATCH_END);
            renderContext->currentBatchId = batchId;
        }
        return 0;
    }

    if (renderContext->currentBatchId != 0) {
        WriteDrawFunc(renderContext->zTree, DRAW_FUNC_BATCH_END);
    }

    renderContext->currentBatchId = batchId;

    if (batchId == 0x7ff) {
        i32 z = CalcZ0(rf);
        AddDrawNode(renderContext->zTree, z, DRAW_FUNC_BATCH_START);
        return 0;
    } else if (batchId == 0x7fe) {
        AddDrawNode(renderContext->zTree, -1, DRAW_FUNC_BATCH_START);
        return 0;
    } else if (batchId == 0x7fd) {
        i32 z = ByteCodeRead16i(rf);
        u8 cl = rf->scale & 0x3f;
        if (cl) {
            z <<= cl;
        }
        z = CalcZ(rf, z);
        AddDrawNode(renderContext->zTree, z, DRAW_FUNC_BATCH_START);
        return 0;
    } else {
        i16 vi = lo8s(batchId);
        VertexData* v = TransformVertex(renderContext, rf, vi);

        u8 upper = (batchId >> 8);
        if (upper & 0x1) {
            if (upper & 0x2) {
                if (upper & 0x4) {
                    i32 z = ByteCodeRead16i(rf);
                    u8 cl = rf->scale & 0x3f;
                    if (cl) {
                        z <<= cl;
                    }
                    z = CalcZVec3i32Offset(rf, v->vVec, z);
                    AddDrawNode(renderContext->zTree, z, DRAW_FUNC_BATCH_START);
                    return 0;
                } else {
                    i32 z1 = CalcZVec3i32(rf, v->vVec);

                    u16 param3;
                    do {
                        param3 = ByteCodeRead16u(rf);
                        i16 vi2 = lo8s(param3);
                        VertexData* v2 = TransformVertex(renderContext, rf, vi2);
                        i32 z2 = CalcZVec3i32(rf, v2->vVec);

                        if (z2 <= z1) {
                            z1 = z2;
                        }
                    } while (param3 & 0x8000);

                    AddDrawNode(renderContext->zTree, z1, DRAW_FUNC_BATCH_START);
                    return 0;
                }
            } else {
                i32 z1 = CalcZVec3i32(rf, v->vVec);

                u16 param3;
                do {
                    param3 = ByteCodeRead16i(rf);
                    i16 vi2 = lo8s(param3);
                    VertexData* v2 = TransformVertex(renderContext, rf, vi2);
                    i32 z2 = CalcZVec3i32(rf, v2->vVec);

                    if (z2 >= z1) {
                        z1 = z2;
                    }
                } while (param3 & 0x8000);

                AddDrawNode(renderContext->zTree, z1, DRAW_FUNC_BATCH_START);
                return 0;
            }
        } else {
            i32 z = CalcZVec3i32(rf, v->vVec);
            AddDrawNode(renderContext->zTree, z, DRAW_FUNC_BATCH_START);
            return 0;
        }
    }
}

// two mirrored tris
MINTERNAL int Render2TriMirrored(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);

    RenderTriParams(renderContext, rf, funcParam, param1 ^ 0x101, param2 ^ 0x101);
    RenderTriParams(renderContext, rf, funcParam, param1, param2);

    return 0;
}

// two mirrored quads
MINTERNAL int Render2QuadMirrored(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param0 = funcParam >> 4;
    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);
    u16 param3 = ByteCodeRead16u(rf);

    RenderQuadParams(renderContext, rf, param0, param1 ^ 0x101, param2 ^ 0x101, param3 ^ 0x1);
    RenderQuadParams(renderContext, rf, param0, param1, param2, param3);

    return 0;
}

// teardrop / flame / engine
MINTERNAL int RenderTeardrop(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param0 = funcParam >> 4;
    u16 colour = CalcColourNoShade(rf, param0);

    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);

    i16 vi1 = lo8s(param1);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);

    if (v1->vVec[2] <= 0x200) {
        return 0;
    }

    i16 vi2 = hi8s(param1);
    VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

    if (v2->vVec[2] <= 0x200) {
        return 0;
    }

    i32 tearSize = param2 * TEARDROP_SCALE;
    i16 scale = rf->scale - 5;
    if (scale <= 0) {
        scale = -scale;
    } else {
        scale &= 0x3f;
    }
    tearSize <<= scale;

    i32 v1z = v1->vVec[2];
    while (v1z > 0x8000) {
        v1z >>= 1;
        tearSize >>= 1;
    }
    tearSize <<= 8;

    i32 blobSize;
    if (tearSize == 0) {
        blobSize = tearSize;
    } else {
        blobSize = tearSize / v1z;
    }

    if (!blobSize) {
        return 0;
    }

    if (blobSize <= 8) {
        RenderLineWithVerts(renderContext, rf, v1, v2, colour);
        return 0;
    }

    i32 dx = v1->sVec.x - v2->sVec.x;
    i32 dy = v1->sVec.y - v2->sVec.y;

    i32 dx2 = dx * dx;
    i32 dy2 = dy * dy;

    i32 vDist2d = FMath_SqrtFunc32(dx2 + dy2);

    if (vDist2d < (blobSize >> 4)) {
        // ???
        RenderLineWithVerts(renderContext, rf, v1, v2, colour);
    } else {
        if (renderContext->currentBatchId) {
            u16* drawFunc = BatchSpanStart(renderContext->zTree);
            renderContext->lastBatchFunc = (DrawFunc*)drawFunc;
        } else {
            i32 z2 = CalcZVec3i32(rf, v2->vVec);
            RasterOpNode *drawNode = AddDrawNode(renderContext->zTree, z2, DRAW_FUNC_SPANS_START);
            renderContext->lastBatchFunc = &(drawNode->func);
        }

        i32 blobAxisX = ((dx << 15) + 0x4000);
        if (vDist2d == 0) {
            if (blobAxisX < 0) {
                blobAxisX = INT_MIN;
            } else {
                blobAxisX = INT_MAX;
            }
        } else {
            blobAxisX = blobAxisX / vDist2d;
        }

        i32 blobAxisY = ((dy << 15) + 0x4000);
        if (vDist2d == 0) {
            if (blobAxisY < 0) {
                blobAxisY = INT_MIN;
            } else {
                blobAxisY = INT_MAX;
            }
        } else {
            blobAxisY = blobAxisY / vDist2d;
        }

        i32 v1Offset = ((vDist2d / 3) + (blobSize >> 4)) << 1;

        i32 v1OffsetX = v1Offset * blobAxisX;
        i32 v1OffsetY = v1Offset * blobAxisY;

        i32 controlPtDisplacementX = blobSize * blobAxisY;
        i32 controlPtDisplacementY = -blobSize * blobAxisX;

        i32 control1XOffset = v1OffsetX - controlPtDisplacementX;
        i32 control1YOffset = v1OffsetY - controlPtDisplacementY;

        i32 control2XOffset = v1OffsetX + controlPtDisplacementX;
        i32 control2YOffset = v1OffsetY + controlPtDisplacementY;

        DrawParamsBezier* drawBezier = BatchSpanBezier(renderContext->zTree);
        drawBezier->pts[0] = v2->sVec;
        drawBezier->pts[1].x = v1->sVec.x + (control1XOffset >> 16);
        drawBezier->pts[1].y = v1->sVec.y + (control1YOffset >> 16);
        drawBezier->pts[2].x = v1->sVec.x + (control2XOffset >> 16);
        drawBezier->pts[2].y = v1->sVec.y + (control2YOffset >> 16);
        drawBezier->pts[3] = v2->sVec;

        DrawParamsColour* endSpans = BatchSpanEnd(renderContext->zTree);
        endSpans->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
    }

    return 0;
}

MINTERNAL int RenderVectorTextOneStack(RenderContext* rc, RenderFrame* rf, u16 param1, u16 normalIndex, u16 colour) {
    u16 normalColour = *(rf->normals + normalIndex);

    u16 param2 = ByteCodeRead16u(rf);

    i16 vi1 = lo8s(param2);
    VertexData* v1 = rf->vertexTrans + vi1;
    TransformVertex(rc, rf, vi1);

    RenderFrame* newRenderFrame = PushRenderFrame(rc);

    newRenderFrame->zScale = rf->zScale;
    newRenderFrame->renderContext = rf->renderContext;

    CopyVertexView(v1, newRenderFrame->objViewVec);
    CopyVertexView(v1, newRenderFrame->lineStartVec);

    newRenderFrame->baseColour = colour;

    u16 param3 = ByteCodeRead16u(rf);
    newRenderFrame->normalOffset = param3;

    newRenderFrame->scale = (param1 >> 8) & (u16)0xf;

    newRenderFrame->parentVertices = rf->parentVertices;

    SetupNewTransformMatrix(rc, newRenderFrame, rf, param2 >> 8);

    if (newRenderFrame->matrixWinding < 0) {
        newRenderFrame->modelViewMatrix[0][0] = -newRenderFrame->modelViewMatrix[0][0];
        newRenderFrame->modelViewMatrix[0][1] = -newRenderFrame->modelViewMatrix[0][1];
        newRenderFrame->modelViewMatrix[0][2] = -newRenderFrame->modelViewMatrix[0][2];
    }

    u16 fontIndex = (param1 >> 12);
    FontModelData* font = GetFontModel(rf->renderContext, fontIndex);

    newRenderFrame->vertexData = (u16*) (((u8*)font) + font->vertexDataOffset);

    newRenderFrame->numVertexModel = font->verticesDataSize / sizeof(VertexData);
    newRenderFrame->numVertexTmp = 0;
    newRenderFrame->numNormals = 4;

    newRenderFrame->modelData = (ModelData*)font;

#ifdef FINSPECTOR
    newRenderFrame->fileDataStartAddress = rf->renderContext->fontModelDataFileStartAddress;
#endif

#ifdef FRAME_MEM_USE_STACK_ALLOC
    newRenderFrame->vertexTrans = (VertexData*)alloca(sizeof(VertexData) * (newRenderFrame->numVertexTmp + newRenderFrame->numVertexModel));
#elif FRAME_MEM_USE_STACK_DARY
    VertexData vData[newRenderFrame->numVertexTmp + newRenderFrame->numVertexModel];
    newRenderFrame->vertexTrans = vData;
#elif FRAME_MEM_USE_MALLOC
    u8* stackMemStart = rc->memStack->pos;
    newRenderFrame->vertexTrans = (VertexData*)MMemStackAlloc(rc->memStack, sizeof(VertexData) * (newRenderFrame->numVertexTmp + newRenderFrame->numVertexModel));
#endif

    for (int i = 0; i <= newRenderFrame->numVertexModel; i++) {
        VertexData* vertex = newRenderFrame->vertexTrans + i;
        vertex->projectedState = 0;
    }

    u32 textBufSize = 0x80;

#ifdef FRAME_MEM_USE_STACK_ALLOC
    i8* stackData = (i8*)alloca(newRenderFrame->numNormals * 2 + textBufSize);
    i8* textBuffer = stackData;

    newRenderFrame->normals = (u16 *)(stackData + textBufSize);
#elif FRAME_MEM_USE_STACK_DARY
    i8 textBuffer[textBufSize];
    u16 normals[newRenderFrame->numNormals];
    newRenderFrame->normals = normals;
#elif FRAME_MEM_USE_MALLOC
    newRenderFrame->normals = (u16 *)MMemStackAlloc(rc->memStack, newRenderFrame->numNormals * sizeof(u16));

    i8* textBuffer = (i8*)MMemStackAlloc(rc->memStack, textBufSize);
#endif

    newRenderFrame->normals[0] = 0;
    newRenderFrame->normals[1] = 0;
    for (int i = 2; i < newRenderFrame->numNormals; i++) {
        newRenderFrame->normals[i] = normalColour;
    }

    newRenderFrame->frameId = 2;

    // Render each char
    Render_LoadFormattedString(rf->renderContext, param3, textBuffer, textBufSize);

    int i = 0;
    u8 c = 0;
    while ((c = textBuffer[i]) != 0) {
        i16 vertexIndex = 0;
        if (c >= 0x20) {
            u16 charModelOffset = c - 0x20;

            if (CheckClipped(rc, newRenderFrame->modelData)) {
                u8* charByteCode = GetFontByteCodeForCharacter(font, charModelOffset);
                newRenderFrame->byteCodePos = charByteCode;
                InterpretModelCode(rc, newRenderFrame);
                i16 val = (*(i16 *)(newRenderFrame->byteCodePos - 2));
                vertexIndex = val >> 6;
            } else {
                u8* charByteCode = GetFontByteCodeForCharacter(font, charModelOffset + 1);
                i16 val = (*(i16 *)(charByteCode - 2));
                vertexIndex = val >> 6;
            }

            if (vertexIndex) {
                VertexData* v = TransformVertex(rc, newRenderFrame, vertexIndex);

                CopyVertexView(v, newRenderFrame->objViewVec);

                newRenderFrame->frameId++;
            }
        } else {
            // do transform
            Vec3i32Copy(newRenderFrame->lineStartVec, newRenderFrame->objViewVec);

            newRenderFrame->frameId++;

            vertexIndex = lo8s(font->newLineVector);
            VertexData* v = newRenderFrame->vertexTrans + vertexIndex;

            TransformVertex(rc, newRenderFrame, vertexIndex);

            CopyVertexView(v, newRenderFrame->objViewVec);
            CopyVertexView(v, newRenderFrame->lineStartVec);

            newRenderFrame->frameId++;
        }
        i++;
    }

#if FRAME_MEM_USE_MALLOC
    MMemStackReset(rc->memStack, stackMemStart);
#endif

    PopRenderFrame(rc);

    return 0;
}

// Render vector text on 2d plane
MINTERNAL int RenderVectorText(RenderContext* rc, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(rc);

    u16 colourParam = (funcParam >> 4) & (u16)0xffe;
    u16 param1 = ByteCodeRead16u(rf);

    u8 normalIndex = param1 & 0x7f;
    u16 colour = CalcNormalColour(rf, colourParam, normalIndex);
    if (colour & 0x8000) {
        ByteCodeSkipBytes(rf, 4);
        return 0;
    }

    return RenderVectorTextOneStack(rc, rf, param1, normalIndex, colour);
}

MINTERNAL int RenderIf_SkipToEnd(RenderFrame* rf, u16 skipParam) {
    u16 skipBytes = (((u16)(skipParam)) >> 4) & (u16)0xffe;
    if (skipBytes == 0) {
        ByteCodeSkipToEnd(rf);
    } else {
        ByteCodeSkipBytes(rf, skipBytes);
    }
    return 0;
}

// Function skips based on is specified normal facing
MINTERNAL int RenderIf(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);
    u16 param1 = ByteCodeRead16u(rf);
    if (param1 & 0x8000) {
        u8 normalIndex = param1 & 0x7f;
        u16* n = rf->normals + normalIndex;
        u16 normalColour = *n;
        if (normalColour == NORMAL_NOT_CALCULATED) {
            normalColour = CalcNormalLightTint(rf, normalIndex);
        }
        if (!(normalColour & NORMAL_FACING_AWAY)) {
            return 0;
        }
    } else if (rf->objViewVec[2] > 0) {
        u16 mode;
        if (param1 & 0x1) {
            mode = renderContext->renderDetail2;
        } else {
            mode = renderContext->renderDetail1;
        }
        mode += rf->scale;
        i32 z = param1 * SCREEN_SCALE;
        while (mode > 0) {
            if (z < 0) {
                return 0;
            }
            z = z * 2;
            mode--;
        }
        z -= rf->objViewVec[2];
        if (z >= 0) {
            return 0;
        }
    } else {
        return 0;
    }

    return RenderIf_SkipToEnd(rf, funcParam);
}

// Function skips based on is specified normal facing
MINTERNAL int RenderIfNot(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);
    u16 param1 = ByteCodeRead16u(rf);
    if (param1 & 0x8000) {
        u8 normalIndex = param1 & 0x7f;
        u16* n = rf->normals + normalIndex;
        u16 normalColour = *n;
        if (normalColour == NORMAL_NOT_CALCULATED) {
            normalColour = CalcNormalLightTint(rf, normalIndex);
        }
        if (normalColour & NORMAL_FACING_AWAY) {
            return 0;
        }
    } else if (rf->objViewVec[2] > 0) {
        u16 mode;
        if (param1 & 0x1) {
            mode = renderContext->renderDetail2;
        } else {
            mode = renderContext->renderDetail1;
        }
        mode += rf->scale;
        i32 z = param1 * SCREEN_SCALE;
        while (mode > 0) {
            if (z < 0) {
                return RenderIf_SkipToEnd(rf, funcParam);
            }
            z = z * 2;
            mode--;
        }
        z -= rf->objViewVec[2];
        if (z < 0) {
            return 0;
        }
    }

    return RenderIf_SkipToEnd(rf, funcParam);
}

// Basic Math funcs
MINTERNAL int RenderCalc(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 data1 = ByteCodeRead16u(rf);
    u16 p1 = GetValueForParam8(rf, data1 & 0xff);
    u16 p2 = GetValueForParam8(rf, data1 >> 8);

    u8 mathFunc = ((u8)(funcParam >> 3 & 0x1e)) >> 1;
    u8 resultOffset = ((u8)(funcParam >> 7 & 0xe)) >> 1;

    u16 u16Result = 0;

    switch (mathFunc) {
        case MathFunc_Add: {
            u16Result = p1 + p2;
            break;
        }
        case MathFunc_Sub: {
            u16Result = p1 - p2;
            break;
        }
        case MathFunc_Mult:
        case MathFunc_Mult2: {
            u16Result = p1 * p2;
            break;
        }
        case MathFunc_Div: {
            if (p2 != 0) {
                u16Result = p1 / p2;
            } else {
                u16Result = 0;
            }
            break;
        }
        case MathFunc_DivPower2: {
            u16Result = p1 >> p2;
            break;
        }
        case MathFunc_MultPower2: {
            u16Result = p1 << p2;
            break;
        }
        case MathFunc_Max: {
            if (p1 > p2) {
                u16Result = p1;
            } else {
                u16Result = p2;
            }
            break;
        }
        case MathFunc_Min: {
            if (p1 < p2) {
                u16Result = p1;
            } else {
                u16Result = p2 - 1;
            }
            break;
        }
        case MathFunc_DivPower2Signed: {
            u16Result = ((i16)p1) / (1u << p2);
            break;
        }
        case MathFunc_RGetIndirectOffset: {
            u16 i = (p1 + p2);
            u16Result = rf->renderContext->modelVars[i];
            break;
        }
        case MathFunc_ZeroIfGreater: {
            if (p1 >= p2) {
                u16Result = p1;
            } else {
                u16Result = 0;
            }
            break;
        }
        case MathFunc_ZeroIfLess: {
            if (p1 < p2) {
                u16Result = p1;
            } else {
                u16Result = 0;
            }
            break;
        }
        case MathFunc_MultSine: {
            u16 mask = 1 << 0xf;
            i32 sine = 0;
            if (p2 & mask) {
                p2 = p2 ^ mask;
                sine = -FMath_sine[p2 >> 4];
            } else {
                sine = FMath_sine[p2 >> 4];
            }

            u16Result = ((sine * p1) >> 15);
            break;
        }
        case MathFunc_MultCos: {
            u16 mask = 1 << 0xf;
            i32 cosine = 0;
            p2 += 0x4000;
            if (p2 & mask) {
                p2 = p2 ^ mask;
                cosine = -FMath_sine[p2 >> 4];
            } else {
                cosine = FMath_sine[p2 >> 4];
            }

            u16Result = ((cosine * p1) >> 15);
            break;
        }
        case MathFunc_And: {
            u16Result = p1 & p2;
            break;
        }
    }

    rf->tmpVariable[resultOffset] = u16Result;

#ifdef FINSPECTOR
    ByteCodeTrace* trace = MArrayTopPtr(*renderContext->byteCodeTrace);
    trace->result = u16Result;
#endif

    return 0;
}

// New scene, conditional on being on-screen
MINTERNAL int RenderModel(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param1 = ByteCodeRead16u(rf);

    i16 vi = lo8s(param1);
    ProjectVertex(renderContext, rf, vi);

    return RenderSubModel(renderContext, rf, funcParam, param1, vi, 0, rf->baseColour);
}

MINTERNAL int RenderAudioCue(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    i32 x = rf->objViewVec[0];
    if (x < 0) {
        x = -x;
    }

    i32 y = rf->objViewVec[1];
    if (y < 0) {
        y = -y;
    }

    i32 z = rf->objViewVec[2];
    if (z < 0) {
        z = -z;
    }

    i32 t = (x + y + z) / 2;
    if (t >= 0x3f0000) {
        return 0;
    }

    t = 0x3f0000 - t;

    MLogf("Audio %d : %d\n", t, funcParam >> 4);

    return 0;
}

MINTERNAL int ProjectBasicConePoints(RenderContext* renderContext, VertexData* v, i32 radius, Vec3i32 nOut, Vec2i16* ptOut) {
    i32 r2 = (F16_MINUS_TWO_THIRDS * radius) >> 15;
    Vec3i32 axis1 = {radius, 0, 0};
    nOut[0] = axis1[0];
    nOut[1] = axis1[1];
    nOut[2] = axis1[2];
    Vec3i32 axis2 = {0, r2, 0};
    return ProjectCircleBezierPoints(renderContext, v, axis1, axis2, ptOut);
}

MINTERNAL int ProjectConePoints(RenderContext* renderContext, VertexData* v, Vec3i32 normal, i32 radius, Vec3i32 nOut, Vec2i16* ptOut) {
    // Normalise view vector
    Vec3i32 vx;
    CopyVertexView(v, vx);

    u8 pow2 = GetScaleBelow(vx, 0x3fff);
    vx[0] >>= pow2;
    vx[1] >>= pow2;
    vx[2] >>= pow2;

    Vec3i32 axis1;
    Vec3i32CrossProd(vx, normal, axis1);

    nOut[0] = axis1[0];
    nOut[1] = axis1[1];
    nOut[2] = axis1[2];

    i32 d = Vec3i32Length(axis1);

    if (d == 0) {
        return ProjectBasicConePoints(renderContext, v, radius, nOut, ptOut);
    }

    i32 s = (radius << 15) + (d >> 1);
    i32 x1 = s / d;

    Vec3i32FractMult(axis1, x1, axis1);

    nOut[0] = axis1[0];
    nOut[1] = axis1[1];
    nOut[2] = axis1[2];

    Vec3i32 axis2;
    Vec3i32CrossProd(normal, axis1, axis2);

    i32 s2 = F16_MINUS_TWO_THIRDS;

    axis2[0] = (axis2[0] * s2) >> 14;
    axis2[1] = (axis2[1] * s2) >> 14;
    axis2[2] = (axis2[2] * s2) >> 14;

    return ProjectCircleBezierPoints(renderContext, v, axis1, axis2, ptOut);
}

MINTERNAL void AddBezierLinePathToBatch(RenderContext* renderContext, BezierSubDivision* subDivision, i32 start) {
    i32 x = subDivision->x;
    i32 y = subDivision->y;

    i32 dx = subDivision->dx;
    i32 dy = subDivision->dy;

    i32 d2x = subDivision->d2x;
    i32 d2y = subDivision->d2y;

    i32 d3x = subDivision->d3x;
    i32 d3y = subDivision->d3y;

    for (i32 i = start; i <= subDivision->steps; i++) {
        x += dx;
        dx += d2x;
        d2x += d3x;

        y += dy;
        dy += d2y;
        d2y += d3y;

        DrawParamsPoint* pt = BatchSpanLineCont(renderContext->zTree);
        pt->x = x >> 16;
        pt->y = y >> 16;
    }

    subDivision->steps = start - 1;

    subDivision->x = x;
    subDivision->y = y;

    subDivision->dx = dx;
    subDivision->dy = dy;

    subDivision->d2x = d2x;
    subDivision->d2y = d2y;
}

MINTERNAL void RenderConeCap(RenderContext* renderContext, u16 lastColour, Vec2i16 bezierPts[6]) {
    DrawParamsColour* draw = BatchSpanDraw(renderContext->zTree);
    draw->colour = Palette_GetIndexFor12bitColour(renderContext->palette, lastColour);

    DrawParamsBezier* bezier1 = BatchSpanBezier(renderContext->zTree);
    bezier1->pts[0] = bezierPts[0];
    bezier1->pts[1] = bezierPts[1];
    bezier1->pts[2] = bezierPts[4];
    bezier1->pts[3] = bezierPts[3];

    DrawParamsBezier* bezier2 = BatchSpanBezier(renderContext->zTree);
    bezier2->pts[0] = bezierPts[3];
    bezier2->pts[1] = bezierPts[5];
    bezier2->pts[2] = bezierPts[2];
    bezier2->pts[3] = bezierPts[0];
}

MINTERNAL void RenderConeCaps(RenderContext* renderContext, RenderFrame* rf, u16 cap1Param, u16 cap2Param,
                              u16 colour1, u16 colour2, u8 normalIndexCap1, Vec2i16* cap1BezierPts, Vec2i16* cap2BezierPts,
                              u16 colourLast) {

    u16 drawCap1 = cap1Param & 0x80;
    if (drawCap1) {
        u16 capColour = CalcNormalColour(rf, colour1, normalIndexCap1);
        if (!(capColour & 0x8000)) {
            RenderConeCap(renderContext, colourLast, cap1BezierPts);
            colourLast = capColour;
        }
    }

    u16 drawCap2 = cap2Param & 0x80;
    if (drawCap2) {
        u8 normalIndexCap2 = cap2Param & 0x7f;
        u16 capColour = CalcNormalColour(rf, colour2, normalIndexCap2);
        if (!(capColour & 0x8000)) {
            RenderConeCap(renderContext, colourLast, cap2BezierPts);
            colourLast = capColour;
        }
    }

    DrawParamsColour* drawEnd = BatchSpanEnd(renderContext->zTree);
    drawEnd->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colourLast);
}

MINTERNAL void RenderConeParams(RenderContext* renderContext, RenderFrame* rf, u16 colour, u16 param1, u16 cap1Param, u16 cap2Param,
                                u16 colour1, u16 colour2) {

    i16 v1i = lo8s(param1);
    VertexData* v1 = ProjectVertex(renderContext, rf, v1i);

    if (v1->vVec[2] < ZCLIPNEAR) {
        return;
    }

    i16 v2i = hi8s(param1);
    VertexData* v2 = ProjectVertex(renderContext, rf, v2i);

    if (v2->vVec[2] < ZCLIPNEAR) {
        return;
    }

    u16 cap1Radius = (cap1Param >> 8) << rf->scale;

    u8 normalIndexCap1 = cap1Param & 0x7f;

    Vec3i32 capNormalM;
    GetNormalVec3i32(rf, normalIndexCap1, capNormalM);

    Vec3i32 capNormalV;
    MultVec3i32Matrix(rf->modelViewMatrix, capNormalM, capNormalV);

    Vec2i16 cap1BezierPts[6]; // 6 unique points needed for end cap 1 : 2 actual + 4 control
    Vec3i32 cap1Axis;
    if (!ProjectConePoints(renderContext, v1, capNormalV, cap1Radius, cap1Axis, cap1BezierPts)) {
        return;
    }

    u16 cap2Radius = (cap2Param >> 8) << rf->scale;
    Vec2i16 cap2BezierPts[6]; // 6 unique points needed for end cap 2 : 2 actual + 4 control
    Vec3i32 cap2Axis;
    if (!ProjectConePoints(renderContext, v2, capNormalV, cap2Radius, cap2Axis, cap2BezierPts)) {
        return;
    }

    Vec3i32 mid;
    mid[0] = (v1->vVec[0] + v2->vVec[0]) >> 1;
    mid[1] = (v1->vVec[1] + v2->vVec[1]) >> 1;
    mid[2] = (v1->vVec[2] + v2->vVec[2]) >> 1;

    if (renderContext->currentBatchId) {
        BatchSpanStart(renderContext->zTree);
    } else {
        i32 depth = CalcZVec3i32(rf, mid);
        AddDrawNode(renderContext->zTree, depth, DRAW_FUNC_SPANS_START);
    }

    if (!(colour & 0x100)) {
        Vec3i32 lightSplitAxis;
        Vec3i32CrossProdNormal(capNormalV, rf->lightV, lightSplitAxis);

        // Calc how much light is perp to the cap (if the cap directly faces the light the rest of the cylinder
        // only gets ambient light - ok - well - it could be a cone but this just an approx).
        i32 light = Vec3i32Length(lightSplitAxis); // sin of the angle between cap normal and light
        if (light >= 0x1000) {
            u16 tintIndex = (((light >> 12) ^ 0x7) & 0xff);
            colour = CalcColourNoShade(rf, colour);

            // Normal lighting on cones seems a little bright, so we modify it to be darker
            u16 colourLit = colour + rf->shadeRamp[2 + (tintIndex / 2)];

            i32 area = (light * cap1Radius) >> 15;

            Vec3i32 cap1Axis2 = { cap1Axis[1], cap1Axis[2], cap1Axis[0] };

            i32 dot1 = Vec3i32DotProd(cap1Axis2, lightSplitAxis);
            i32 r = dot1 / (area + 3);

            i16 lightCutoffAngle = FMath_arccos[(r >> 9) & 0x7f];

            i32 dot2 = Vec3i16i32DotProd(rf->lightV, cap1Axis);
            if (dot2 < 0) {
                lightCutoffAngle = -lightCutoffAngle;
                MSWAP(colourLit, colour, u16);
            }

            DrawParamsLine* lineParams = BatchSpanLine(renderContext->zTree);
            lineParams->x1 = cap1BezierPts[0].x;
            lineParams->y1 = cap1BezierPts[0].y;
            lineParams->x2 = cap2BezierPts[0].x;
            lineParams->y2 = cap2BezierPts[0].y;

            Vec2i16 pts[4];
            pts[0] = cap2BezierPts[0];
            pts[1] = cap2BezierPts[1];
            pts[2] = cap2BezierPts[4];
            pts[3] = cap2BezierPts[3];
            BezierSubDivision subDivision1 = InitBezierSubdivision(pts);

            u16 inUnsigned = (u16) lightCutoffAngle;
            i32 end = inUnsigned >> subDivision1.scaled;

            AddBezierLinePathToBatch(renderContext, &subDivision1, end);

            DrawParamsPoint* spanPoint = BatchSpanPoint(renderContext->zTree);
            spanPoint->x = cap1BezierPts[0].x;
            spanPoint->y = cap1BezierPts[0].y;

            pts[0] = cap1BezierPts[0];
            pts[1] = cap1BezierPts[1];
            pts[2] = cap1BezierPts[4];
            pts[3] = cap1BezierPts[3];
            BezierSubDivision subDivision2 = InitBezierSubdivision(pts);

            end = inUnsigned >> subDivision2.scaled;
            AddBezierLinePathToBatch(renderContext, &subDivision2, end);

            DrawParamsPoint* spanCont = BatchSpanLineCont(renderContext->zTree);
            spanCont->x = subDivision1.x >> 16;
            spanCont->y = subDivision1.y >> 16;

            DrawParamsColour* draw = BatchSpanDraw(renderContext->zTree);
            draw->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);

            DrawParamsLine* line = BatchSpanLine(renderContext->zTree);
            line->x1 = subDivision1.x >> 16;
            line->y1 = subDivision1.y >> 16;
            line->x2 = subDivision2.x >> 16;
            line->y2 = subDivision2.y >> 16;

            AddBezierLinePathToBatch(renderContext, &subDivision2, 0);

            spanCont = BatchSpanLineCont(renderContext->zTree);
            spanCont->x = cap2BezierPts[3].x;
            spanCont->y = cap2BezierPts[3].y;

            spanPoint = BatchSpanPoint(renderContext->zTree);
            spanPoint->x = subDivision1.x >> 16;
            spanPoint->y = subDivision1.y >> 16;

            AddBezierLinePathToBatch(renderContext, &subDivision1, 0);

            RenderConeCaps(renderContext, rf, cap1Param, cap2Param, colour1, colour2, normalIndexCap1,
                           cap1BezierPts, cap2BezierPts, colourLit);

            return;
        }
    }

    DrawParamsBezier* bezier1 = BatchSpanBezier(renderContext->zTree);
    bezier1->pts[0] = cap1BezierPts[0];
    bezier1->pts[1] = cap1BezierPts[1];
    bezier1->pts[2] = cap1BezierPts[4];
    bezier1->pts[3] = cap1BezierPts[3];

    DrawParamsPoint* lineCont = BatchSpanLineCont(renderContext->zTree);
    lineCont->x = cap2BezierPts[3].x;
    lineCont->y = cap2BezierPts[3].y;

    DrawParamsBezier* bezier2 = BatchSpanBezier(renderContext->zTree);
    bezier2->pts[0] = cap2BezierPts[0];
    bezier2->pts[1] = cap2BezierPts[1];
    bezier2->pts[2] = cap2BezierPts[4];
    bezier2->pts[3] = cap2BezierPts[3];

    DrawParamsLine* lineEnd = BatchSpanLine(renderContext->zTree);
    lineEnd->x1 = cap1BezierPts[0].x;
    lineEnd->y1 = cap1BezierPts[0].y;
    lineEnd->x2 = cap2BezierPts[0].x;
    lineEnd->y2 = cap2BezierPts[0].y;

    RenderConeCaps(renderContext, rf, cap1Param, cap2Param, colour1, colour2, normalIndexCap1, cap1BezierPts,
                   cap2BezierPts, colour);
}

MINTERNAL int RenderCone(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 colour = CalcColourNoShadeNoBase(rf, (funcParam >> 4));

    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);
    u16 param3 = ByteCodeRead16u(rf);

    RenderConeParams(renderContext, rf, colour, param1, param2, param3, 0, 0);

    return 0;
}

MINTERNAL int RenderConeCapped(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 colour = CalcColourNoShadeNoBase(rf, (funcParam >> 4));

    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);
    u16 param3 = ByteCodeRead16u(rf);
    u16 param4 = ByteCodeRead16u(rf);
    u16 param5 = ByteCodeRead16u(rf);

    RenderConeParams(renderContext, rf, colour, param1, param2, param3, param4, param5);

    return 0;
}

// Skip if (bit) clear/false
MINTERNAL int RenderIfVar(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);
    u16 param1 = ByteCodeRead16u(rf);
    i16 val = GetValueForParam8(rf, param1 & 0xff);
    i16 bit = param1 >> 8;

    if (bit) {
        bit--;
        if (val & (1 << bit)) {
            return 0;
        }
    } else if (val) {
        return 0;
    }

    u16 skipBytes = (funcParam >> 4) & 0xfffe;
    if (skipBytes == 0) {
        ByteCodeSkipToEnd(rf);
    } else {
        ByteCodeSkipBytes(rf, skipBytes);
    }

    return 0;
}

// Skip if (bit) set/true
MINTERNAL int RenderIfNotVar(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);
    u16 param1 = ByteCodeRead16u(rf);
    i16 val = GetValueForParam8(rf, param1 & 0xff);
    i16 bit = param1 >> 8;

    if (bit) {
        bit--;
        if (!(val & (1 << bit))) {
            return 0;
        }
    } else {
        if (!val) {
            return 0;
        }
    }

    u16 skipBytes = (funcParam >> 4) & 0xfffe;
    if (skipBytes == 0) {
        ByteCodeSkipToEnd(rf);
    } else {
        ByteCodeSkipBytes(rf, skipBytes);
    }
    return 0;
}

// Rebases the draw tree
MINTERNAL int RenderClearZ(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    if (funcParam & 0x8000) {
        // pop unbase
        ZTree_PopSubTree(renderContext->zTree);
        return 0;
    }

    u16 param0 = funcParam >> 5;

    i16 vi = param0 & 0xff;
    VertexData* v = TransformVertex(renderContext, rf, vi);

    i32 z = CalcZVec3i32(rf, v->vVec);

    ZTree_PushSubTree(renderContext->zTree, z);

    return 0;
}

// Bezier line
MINTERNAL int RenderLineBezier(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 colourParam = funcParam >> 4;
    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);
    u16 param3 = ByteCodeRead16u(rf);

    u8 normalIndex = param3 & 0x7f;
    u16 colour = CalcNormalColour(rf, colourParam, normalIndex);
    if (colour & 0x8000) {
        return 0;
    }

    i16 vi3 = lo8s(param1);
    VertexData* v3 = ProjectVertex(renderContext, rf, vi3);

    i16 vi1 = hi8s(param1);
    VertexData* v1 = ProjectVertex(renderContext, rf, vi1);

    i16 vi4 = lo8s(param2);
    VertexData* v4 = ProjectVertex(renderContext, rf, vi4);

    i16 vi2 = hi8s(param2);
    VertexData* v2 = ProjectVertex(renderContext, rf, vi2);

    if (v2->vVec[2] < ZCLIPNEAR || v4->vVec[2] < ZCLIPNEAR || v2->vVec[2] < ZCLIPNEAR || v1->vVec[2] < ZCLIPNEAR) {
        return 0;
    }

    DrawParamsBezierColour* bezier = NULL;
    if (renderContext->currentBatchId) {
        bezier = BatchBezierLine(renderContext->zTree);
    } else {
        i32 z = CalcZVec3i32(rf, v1->vVec);
        bezier = AddBezierLineNode(renderContext->zTree, z);
    }
    bezier->pts[0] = v1->sVec;
    bezier->pts[1] = v2->sVec;
    bezier->pts[2] = v3->sVec;
    bezier->pts[3] = v4->sVec;
    bezier->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);

    return 0;
}

// Skip if two vertices are within / past a certain distance in view coordinates
// Test skipped if any vertex is behind camera
MINTERNAL int RenderViewspaceDist(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param1 = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);

    i16 v1i = lo8s(param2);
    VertexData* v1 = ProjectVertex(renderContext, rf, v1i);
    if (v1->vVec[2] < ZCLIPNEAR) {
        return 0;
    }

    i16 v2i = hi8s(param2);
    VertexData* v2 = ProjectVertex(renderContext, rf, v2i);
    if (v2->vVec[2] < ZCLIPNEAR) {
        return 0;
    }

    i32 x = (i32)v2->sVec.x - (i32)v1->sVec.x;
    if (x < 0) {
        x = -x;
    }

    i32 y = (i32)v2->sVec.y - (i32)v1->sVec.y;
    if (y < 0) {
        y = -y;
    }

    i32 d = (x + y);
    if (param1 & 0x8000) {
        i32 min = param1 ^ 0x8000;
        if (d > min) {
            return 0;
        }
    } else {
        if (d <= param1) {
            return 0;
        }
    }

    u16 skipBytes = (funcParam >> 4) & 0xfffe;
    if (skipBytes == 0) {
        ByteCodeSkipToEnd(rf);
    } else {
        ByteCodeSkipBytes(rf, skipBytes);
    }

    return 0;
}

MINTERNAL int RenderBalls(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param0 = (funcParam >> 4);
    u16 param1 = ByteCodeRead16u(rf);

    u16 colour = CalcColourMidTint(rf, param0);

    u32 p1 = GetValueForParam16(rf, param1);

    i32 width = 0;

    if (p1 & 0x8000) {
        width = p1 ^ 0x8000;
    } else {
        i16 cl = rf->scale - 7;

        if (cl >= 0) {
            cl &= 0x3f;
            if (cl) {
                p1 <<= cl;
            }
        } else {
            cl = -cl;
            p1 >>= cl;
        }

        i32 z = rf->objViewVec[2];
        if (z <= 0) {
            while (TRUE) {
                i8 param = ByteCodeRead8i(rf);
                if (param == 0x7f) {
                    break;
                }
            }
            return 0;
        }

        while (z > 0x8000) {
            p1 >>= 1;
            z >>= 1;
        }

        width = (p1 << 8) / z;
    }

    DrawParamsBalls* drawParams;

    if (renderContext->currentBatchId != 0) {
        drawParams = BatchBalls(renderContext->zTree);
    } else {
        i32 z = CalcZ0(rf);
        drawParams = AddBallsNode(renderContext->zTree, z);
    }

    drawParams->width = width;
    drawParams->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);

    int i = 0;
    while (TRUE) {
        u16 param = ByteCodeRead16u(rf);

        i16 vi = hi8s(param);
        if (vi == 0x7f) {
            break;
        }

        VertexData* v1 = ProjectVertex(renderContext, rf, vi);

        if (v1->vVec[2] >= ZCLIPNEAR) {
            i16 x = v1->sVec.x - 1;
            i16 y = v1->sVec.y - 1;

            if (IsCircleVisible(x, y, width / 2, renderContext->width, renderContext->height)) {
                drawParams->pos[i++] = x;
                drawParams->pos[i++] = y;
            }
        }

        vi = lo8s(param);
        if (vi == 0x7f) {
            break;
        }

        VertexData* v2 = ProjectVertex(renderContext, rf, vi);

        if (v2->vVec[2] >= ZCLIPNEAR) {
            i16 x = v2->sVec.x - 1;
            i16 y = v2->sVec.y - 1;

            if (IsCircleVisible(x, y, width / 2, renderContext->width, renderContext->height)) {
                drawParams->pos[i++] = x;
                drawParams->pos[i++] = y;
            }
        }
    }

    drawParams->num = i;

    renderContext->zTree->offset += (i << 1);

    return 0;
}

MINTERNAL int RenderMatrixSetup(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    u16 param0 = (funcParam) >> 4;
    if (param0 & 0x800) {
        Matrix3x3i16Identity(rf->modelMatrix);
        return 0;
    }

    // Orientate axis to light source
    u16 rotationAxis = (param0 >> 1) & 0x3;
    u16 row1 = sXyzRotTableOffsets[rotationAxis];
    u16 row2 = sXyzRotTableOffsets[rotationAxis + 1];
    u16 o1 = sMatrixOffsets3[rotationAxis + 1];
    i32 a = rf->lightM[o1];
    o1 = sMatrixOffsets3[o1];
    i32 b = rf->lightM[o1];

    i32 sq = a * a + b * b;
    i32 r = FMath_SqrtFunc32(sq);
    r *= 0x8101;

    i16 sine = 0;
    i16 cosine = 0;

    if (r >> 16 == 0) {
        u16 angle = r & 0xffff;
        LookupSineAndCosine(angle, &sine, &cosine);
    } else {
        cosine = a << 14 / r;
        sine = b << 14 / r;
    }

    RotateMatrix(rf->modelMatrix, row1, row2, sine, cosine);
    return 0;
}

// Set default colour
MINTERNAL int RenderColour(RenderContext* scene, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(scene);

    u16 param0 = (funcParam) >> 4;
    u16 param1 = ByteCodeRead16u(rf);

    u16 colourListProvided = param1 >> 8;
    u16 colourParam = param0 & 0xffe;
    if (colourListProvided) {
        u16 p1 = GetValueForParam8(rf, colourListProvided);
        p1 &= 0x7;
        u32 skipBytes;
        if (p1) {
            ByteCodeSkipBytes(rf, (p1-1) * 2);
            colourParam = ByteCodeRead16u(rf);
            skipBytes = 14 - (p1 * 2);
        } else {
            skipBytes = 14;
        }

        ByteCodeSkipBytes(rf, skipBytes);
    }

    u8 normalIndex = param1 & 0x7f;
    if (param1 & 0x80) {
        u16 colour = CalcNormalColour(rf, colourParam, normalIndex);
        rf->normals[0] = colour;
    } else {
        u16 colour = CalcNormalColour(rf, colourParam, normalIndex);
        rf->baseColour = colour;
    }

    return 0;
}

MINTERNAL int RenderModelScale(RenderContext* scene, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(scene);

    u16 param1 = ByteCodeRead16u(rf);

    i16 vi = lo8s(param1);
    ProjectVertex(scene, rf, vi);

    u16 param2 = ByteCodeRead16u(rf);
    u16 colourParam = (param2 << 1) & 0xfff;
    u16 baseColour = CalcNewBaseColour(rf, colourParam);

    i16 scale;
    if (param2 & 0x800) {
        u16 ix = param2 >> 0xc;
        scale = rf->tmpVariable[ix];
    } else {
        scale = ((i16)(param2)) >> 0xc;
    }

    return RenderSubModel(scene, rf, funcParam, param1, vi, scale, baseColour);
}

void Matrix3x3i16Rotate(Matrix3x3i16 matrix, u16 rotationAxis, u16 angle) {
    u16 row1 = sXyzRotTableOffsets[rotationAxis];
    u16 row2 = sXyzRotTableOffsets[rotationAxis + 1];

    i16 sine = 0;
    i16 cosine = 0;
    LookupSineAndCosine(angle, &sine, &cosine);

    RotateMatrix(matrix, row1, row2, sine, cosine);
}

MINTERNAL int RenderMatrixTransform(RenderContext* scene, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(scene);

    u16 param1 = ByteCodeRead16u(rf);

    if (!(funcParam & (u16) 0x8000)) {
        rf->newMatrixWinding = FlipMatrixAxis(rf, rf->modelMatrix, (funcParam >> 7) & 0xff);

        if (!param1) {
            return 0;
        }
    }

    u16 rotationAxis = (funcParam >> 5) & (u16) 0x3;
    u16 angle = GetValueForParam16(rf, param1);

    Matrix3x3i16Rotate(rf->modelMatrix, rotationAxis, angle);

    return 0;
}

MINTERNAL int RenderMatrixCopy(RenderContext* scene, u16 funcParam) {
    u16 param0 = funcParam >> 4;
    RenderFrame* rf = GetRenderFrame(scene);

    if (param0 & 0x800) {
        if (param0 == 0x801) {
            MLogf("Unhandled matrix setup %d 1", param0);
            return 0;
        } else {
            if (param0 == 0xc01) {
                Matrix3i16Copy(rf->modelMatrix, rf->modelViewMatrix);
            } else {
                MLogf("Unhandled matrix setup %d 2", param0);
                Matrix3i16Copy(rf->modelMatrix, rf->modelViewMatrix);
            }
            return 0;
        }
    } else {
        u16 p1 = GetValueForParam8(rf, param0);
        MLogf("Unhandled matrix setup %d 3", param0);
    }
    return 0;
}

typedef struct sBodyPoint {
    Vec3i16 pos;

    //  0x09 - point on far side of sphere
    // -0x79 - point not clipped
    i16 clip;
    Vec2i16 pt;
} BodyPoint;

typedef struct sBodyWorkspace {
    u16 colours[16];

    Float16 centerX;
    Float16 centerY;
    Float16 centerZ;

    Float16 radius;

    Float16 tangentZ2; // x squared + y squared

    // Dist to nearest point on sphere
    Float16 tangentDepth;

    i16 axisX;
    i16 axisY;

    i16 radiusScaled;

    // Viewspace vector to center of planet (this vector is also scaled via radiusScaled above)
    Vec3i16 center;

    i16 arcRadius;

    i16 radiusFeatureDraw;

    Vec3i16 q;

    BodyPoint prevPt;
    BodyPoint endPt;

    i16 detailLevel; // renderPolyMode2 + 16

    i16 detailParam;

    i16 y1Last; // last xor written
    i16 y1UpdateDir;
    u16 y1UpdateCount;
    i16 y2Last;
    i16 yDir2;
    u16 yMaxHit;
    i16 outsideSphere;

    // 4 bit colour mode
    // bit 0 : Read from first 4 colours from list of 8 colour schemes based off parameter
    // bit 1 : Extra 3rd shading ring if bit 2 is set
    // bit 2 : Extra 2nd shading ring if bit 3 is set
    // bit 3 : Apply 1st shade ring - always use 8 shading colours from first 8
    u16 colourMode;

    u16 radiusScale;
    u8* initialCodeOffset; // after first three words

    i16 xLast;
    i16 doneRenderFeatures;

    u16 lastTint;

    i16 distLog2;
    u16 baseColour;

    i8 isMonoColour;
    u16 startColour; // start colour for first span (bottom, spans are rendered bottom to top)

    u32 random;
    u16 colour;
} BodyWorkspace;

MINTERNAL void CalcSkyColour(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace) {
    // Normalising center vector is expensive and not done in the original
    Vec3i16 normalisedCenter;
    Vec3i16Normalise(workspace->center, normalisedCenter);

    i32 light = Vec3i16DotProd(rf->lightV, normalisedCenter);

    i32 lightFinal = -(light >> 26);
    if (lightFinal < -3) {
        lightFinal = -3;
    } else if (lightFinal > 3) {
        lightFinal = 3;
    }

    workspace->lastTint = rf->shadeRamp[lightFinal + 3];
    if (lightFinal > 0 || workspace->distLog2 < 5) {
        return;
    }

    PaletteContext* palette = scene->palette;
    u16 colour = workspace->baseColour + workspace->lastTint;
    palette->backgroundColour = colour;
}

MINTERNAL void PlanetWriteBezierNode(RenderContext* scene, RenderFrame* rf, VertexData* v, const Vec2i16 bezierPt[4]) {
    if (!scene->currentBatchId) {
        const i32 minDist = 0x2625a00;
        i32 z1 = v->vVec[2];
        if (v->vVec[2] < minDist) {
            v->vVec[2] = minDist;
        }
        i32 z = CalcZVec3i32(rf, v->vVec);
        AddDrawNode(scene->zTree, z, DRAW_FUNC_BODY_START);
        v->vVec[2] = z1;
    } else {
        BatchBodySpanStart(scene->zTree);
    }

    DrawParamsBezierColour* drawBezier = BatchBodyBezier(scene->zTree);
    drawBezier->pts[0] = bezierPt[0];
    drawBezier->pts[1] = bezierPt[1];
    drawBezier->pts[2] = bezierPt[2];
    drawBezier->pts[3] = bezierPt[3];
    drawBezier->colour = 0;
}

MINTERNAL void PlanetProjectPoint(BodyWorkspace* workspace, Vec3i16 vec, BodyPoint* result) {
    u32 d = Vec3i16Length(vec);

    if (d == 0) {
        Vec3i16Copy(vec, result->pos);

        result->clip = 0x11;

        result->pt.x = vec[0];
        result->pt.y = vec[1];

        return;
    }

    i16 c = 0;
    while (!(d & 0x8000)) {
        c++;
        d <<= 1;
    }

    i32 e = workspace->arcRadius << 16;
    i32 f = e / d;

    Vec3i32 wVec;
    wVec[0] = (f * vec[0]);
    wVec[1] = (f * vec[1]);
    wVec[2] = (f * vec[2]);

    c = 16 - c;
    if (c) {
        wVec[0] >>= c;
        wVec[1] >>= c;
        wVec[2] >>= c;
    }

    Vec3i16 sVec;
    sVec[0] = wVec[0] + workspace->q[0];
    sVec[1] = wVec[1] + workspace->q[1];
    sVec[2] = wVec[2] + workspace->q[2];

    Vec3i16 pVec;
    Vec3i16Add(sVec, workspace->center, pVec);

    // Check if point on sphere faces camera
    i32 s = Vec3i16DotProd(sVec, pVec);
    if (s < 0) {
        // it does
        result->clip = -0x7f;
    } else {
        // it doesn't
        if (wVec[2] < 0) {
            // point faces away AND also the direction we got there from faces us
            result->clip = 0x27;
        } else {
            Vec3i16Copy32(wVec, result->pos);

            result->clip = 0x9;

            result->pt.x = wVec[0];
            result->pt.y = wVec[1];

            return;
        }
    }

    // screen space coordinate is behind viewer
    if (pVec[2] < 0 /* ZCLIPNEAR */) {
        result->clip = 0x11;

        Vec3i16Copy32(wVec, result->pos);

        result->pt.x = wVec[0];
        result->pt.y = wVec[1];

        return;
    }

    Vec2i16 point = ScreenCoords(ZProjecti16(pVec));
    result->pt = point;

    point.x += PLANET_CLIP_BORDER;
    point.y += PLANET_CLIP_BORDER;
    if ((point.x < 0) || (point.x > (SURFACE_WIDTH + (2 * PLANET_CLIP_BORDER)))) {
        result->clip = (result->clip & 0x21) | 0x40;
        if ((point.y < 0) || (point.y > (SURFACE_HEIGHT + (2 * PLANET_CLIP_BORDER)))) {
            result->clip |= 0x4;
        }
    } else if ((point.y < 0) || (point.y > (SURFACE_HEIGHT + (2 * PLANET_CLIP_BORDER)))) {
        result->clip = (result->clip & 0x21) | 0x42;
    }

    Vec3i16Copy32(wVec, result->pos);
}

MINTERNAL void PlanetArcStart(BodyWorkspace* workspace, Vec3i16 vec) {
    workspace->y1Last = -(PLANET_CLIP_BORDER / 2);
    workspace->y1UpdateDir = 0;
    workspace->y1UpdateCount = 0;

    workspace->y2Last = -(PLANET_CLIP_BORDER / 2);
    workspace->yDir2 = 0;
    workspace->yMaxHit = 0;

    PlanetProjectPoint(workspace, vec, &workspace->prevPt);

    workspace->endPt = workspace->prevPt;
}

static u16 sPlanetSegmentBreakpoints[] = {
        1000, 1000, 1000, 20,
        20, 15, 12, 5,
        4, 4, 4, 4
};

MINTERNAL void PlanetArcProject(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 spokeVec);
MINTERNAL void PlanetFeatureLineSegSplit(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 spokeVec, Vec2i16 pt, i16 clip);

MINTERNAL void PlanetAddLine(RenderContext* scene, BodyWorkspace* workspace, Vec3i16 vec, Vec2i16 pt, i16 clip) {
    DrawParamsLineColour* drawLine = BatchBodyLine(scene->zTree);
    drawLine->x1 = workspace->prevPt.pt.x;
    drawLine->y1 = workspace->prevPt.pt.y;
    drawLine->x2 = pt.x;
    drawLine->y2 = pt.y;
    drawLine->colour = workspace->colour;

    Vec3i16Copy(vec, workspace->prevPt.pos);
    workspace->prevPt.clip = clip;
    workspace->prevPt.pt = pt;

    workspace->outsideSphere = 0;
}

MINTERNAL void PlanetFeatureAddLineSeg(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 vec, Vec2i16 pt, i16 clip) {
    workspace->random = workspace->random + ((workspace->random >> 16) + ((workspace->random & 0xffff) << 16));

    if (clip < 0) {
        // not clipped
        if (workspace->prevPt.clip < 0) {
            // prev point also not clipped
            i16 x = workspace->prevPt.pt.x - pt.x;
            if (x < 0) {
                x = -x;
            }

            i16 y = workspace->prevPt.pt.y - pt.y;
            if (y < 0) {
                y = -y;
            }

            // crude distance check to see if we need to subdivide further
            u16 dist = x + y;
            u16 segmentLen = sPlanetSegmentBreakpoints[workspace->detailLevel / 2];
            if (dist <= segmentLen) {
                PlanetAddLine(scene, workspace, vec, pt, clip);
                return;
            }
        }

        PlanetFeatureLineSegSplit(scene, rf, workspace, vec, pt, clip);
    } else {
        if (workspace->prevPt.clip < 0) {
            PlanetFeatureLineSegSplit(scene, rf, workspace, vec, pt, clip);
        } else {
            if (workspace->prevPt.clip != clip || clip & 0x40) {
                u16 clip2 = clip | workspace->prevPt.clip;
                if (clip2 & 0x10) {
                    PlanetFeatureLineSegSplit(scene, rf, workspace, vec, pt, clip);
                    return;
                }
                if (clip2 & 0x20) {
                    if (workspace->detailLevel < 10 || clip2 & 0x8) {
                        Vec3i16Copy(vec, workspace->prevPt.pos);
                        workspace->prevPt.clip = clip;
                        workspace->prevPt.pt = pt;
                        return;
                    }
                }
                if (!(clip2 & 0x2)) {
                    if ((~(workspace->prevPt.pt.x ^ pt.x)) & 0x8000) {
                        Vec3i16Copy(vec, workspace->prevPt.pos);
                        workspace->prevPt.clip = clip;
                        workspace->prevPt.pt = pt;
                        return;
                    }
                }
                if (clip2 & 0x4) {
                    PlanetFeatureLineSegSplit(scene, rf, workspace, vec, pt, clip);
                } else {
                    if ((~(workspace->prevPt.pt.y ^ pt.y)) & 0x8000) {
                        Vec3i16Copy(vec, workspace->prevPt.pos);
                        workspace->prevPt.clip = clip;
                        workspace->prevPt.pt = pt;
                    } else {
                        PlanetFeatureLineSegSplit(scene, rf, workspace, vec, pt, clip);
                    }
                }
            } else {
                Vec3i16Copy(vec, workspace->prevPt.pos);
                workspace->prevPt.clip = clip;
                workspace->prevPt.pt = pt;
            }
        }
    }
}

MINTERNAL void PlanetArcEnd(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace) {
    PlanetFeatureAddLineSeg(scene, rf, workspace, workspace->endPt.pos, workspace->endPt.pt, workspace->endPt.clip);

    u16 d4 = workspace->startColour;

    if (workspace->doneRenderFeatures) {
        d4 = workspace->startColour;
    }

    // Try shining light directly on sphere, or behind

    if (workspace->y1UpdateDir == 0) {
        if (workspace->yDir2 == 0) {
            if (workspace->outsideSphere) {
                workspace->startColour ^= workspace->colour;
            }
        } else if (workspace->yDir2 < 0) {
            workspace->isMonoColour = 0;
        } else {
            workspace->isMonoColour = 0;
            if (workspace->yMaxHit & 1 || workspace->doneRenderFeatures) {
                workspace->startColour ^= workspace->colour;
            }
        }
    } else if (workspace->y1UpdateDir < 0) {
        workspace->isMonoColour = 0;
        if (workspace->y1UpdateCount & 1 || workspace->doneRenderFeatures) {
            workspace->startColour ^= workspace->colour;
        }
    } else {
        workspace->isMonoColour = 0;
    }
}

MINTERNAL void PlanetCircle3(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 vec, i16 arcRadius, i16 visible) {
    workspace->outsideSphere = visible;
    workspace->arcRadius = arcRadius;

    // Cross product: A x B (vec)
    // A - { -vec[1], vec[0], 0 }
    // B - {  vec[0], vec[1], vec[2] }
    Vec3i16 p;
    p[0] = ((i32)vec[0] * vec[2]) >> 15;
    p[1] = ((i32)vec[1] * vec[2]) >> 15;
    i16 x2 = ((i32)vec[0] * vec[0]) >> 15;
    i16 y2 = ((i32)vec[1] * vec[1]) >> 15;
    p[2] = -(x2 + y2);

    // Get a vector on the projected circle, by clearing z
    // This is 'A' above
    Vec3i16 vec2;
    vec2[0] = -vec[1];
    vec2[1] = vec[0];
    vec2[2] = 0;

    PlanetArcStart(workspace, vec2);

    vec2[0] = -p[0];
    vec2[1] = -p[1];
    vec2[2] = -p[2];

    PlanetArcProject(scene, rf, workspace, vec2);

    // -A
    vec2[0] = vec[1];
    vec2[1] = -vec[0];
    vec2[2] = 0;

    PlanetArcProject(scene, rf, workspace, vec2);

    PlanetArcProject(scene, rf, workspace, p);

    PlanetArcEnd(scene, rf, workspace);
}

MINTERNAL void PlanetCircle2(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 vec, i16 arcRadius, i16 arcOffset) {
    i32 d = Vec3i16DotProd(workspace->center, vec) >> 15;

    i16 visible = -1;
    if (d <= arcOffset) {
        visible = 0;
    }

    Vec3i16Multi16(vec, -arcOffset, workspace->q);

    PlanetCircle3(scene, rf, workspace, vec, arcRadius, visible);
}

//    |-w-|
//      ___
//    /__|  \     _
//   /   |   \    |
//  |    |    |   q
// |     |     |  |
//       |        -

MINTERNAL void PlanetCircle(RenderContext* scene, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 spokeVec, i16 arcRadius, i16 arcOffset) {
    if (arcOffset > arcRadius) {
        // covers less than half a hemisphere
        PlanetCircle2(scene, rf, workspace, spokeVec, arcRadius, arcOffset);
    } else if (workspace->radiusScaled < 0x1000) {
        Vec3i16Multi16(spokeVec, -arcOffset, workspace->q);

        PlanetCircle3(scene, rf, workspace, spokeVec, arcRadius, 0);
    } else {
        Vec3i16Multi16(spokeVec, -arcOffset, workspace->q);

        Vec3i16 vec2;
        Vec3i16Copy(spokeVec, vec2);
        vec2[0] += workspace->center[0];
        if (vec2[0] < 0) {
            vec2[0] = -vec2[0];
        }
        vec2[1] += workspace->center[1];
        if (vec2[1] < 0) {
            vec2[1] = -vec2[1];
        }
        vec2[2] += workspace->center[2] + 0x200;
        if (vec2[2] < 0) {
            vec2[2] = -vec2[2];
        }

        u32 dist = vec2[0] + vec2[1] + vec2[2];
        if (FMath_RangedCheck(arcRadius, dist)) {
            PlanetCircle3(scene, rf, workspace, spokeVec, vec2[0], 0);
        } else {
            PlanetCircle3(scene, rf, workspace, spokeVec, vec2[0], -1);
        }
    }
}

MINTERNAL void PlanetArcProject(RenderContext* renderContext, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 spokeVec) {
    BodyPoint bodyPoint;
    PlanetProjectPoint(workspace, spokeVec, &bodyPoint);

    PlanetFeatureAddLineSeg(renderContext, rf, workspace, bodyPoint.pos, bodyPoint.pt, bodyPoint.clip);
}

MINTERNAL void PlanetFeatureLineSegSplit(RenderContext* renderContext, RenderFrame* rf, BodyWorkspace* workspace, Vec3i16 spokeVec, Vec2i16 pt, i16 clip) {
    u32 random = workspace->random;
    workspace->detailLevel -= 2;

    if (workspace->detailLevel >= 0) {
        i16 detailParam = workspace->detailParam;
        if (detailParam == 0) {
            // Add prev point to current point, giving a vector who's direction bisects the arc between
            // the two points (both prev and current point is already on the sphere).
            // That is, the sum of two vectors is equal to the diagonal of the parallelogram spanned by the vectors.
            // Since both vectors are the same length, the resulting vector bisects the angle between them (it will
            // however have to be normalised after).
            Vec3i16 vec2;
            Vec3i16Add(spokeVec, workspace->prevPt.pos, vec2);

            PlanetArcProject(renderContext, rf, workspace, vec2);
            PlanetFeatureAddLineSeg(renderContext, rf, workspace, spokeVec, pt, clip);

            workspace->random = random;
            workspace->detailLevel += 2;
            return;
        } else {
            detailParam -= workspace->detailLevel;
            if (detailParam < 20) {
                Vec3i16 vec2;
                Vec3i16Add(spokeVec, workspace->prevPt.pos, vec2);

                detailParam >>= 1;

                u16 a = workspace->random >> 16;
                u16 b = workspace->random & 0xffff;
                workspace->random = workspace->random + (u32) a + ((u32) (b) << 16);

                if (b & 0x8000) {
                    detailParam++;
                }

                a <<= 1;
                if (a & 0x8000) {
                    Vec3i16 vec3;
                    Vec3i16Copy(rf->modelViewMatrix[0], vec3);
                    a <<= 1;
                    if (a & 0x8000) {
                        Vec3i16Neg(vec3);
                    }
                    Vec3i16ShiftRight(vec3, detailParam);
                    Vec3i16Add(vec3, vec2, vec2);
                }

                a <<= 1;
                if (a & 0x8000) {
                    Vec3i16 vec3;
                    Vec3i16Copy(rf->modelViewMatrix[1], vec3);
                    a <<= 1;
                    if (a & 0x8000) {
                        Vec3i16Neg(vec3);
                    }
                    Vec3i16ShiftRight(vec3, detailParam);
                    Vec3i16Add(vec3, vec2, vec2);
                }

                a <<= 1;
                if (a & 0x8000) {
                    Vec3i16 vec3;
                    Vec3i16Copy(rf->modelViewMatrix[2], vec3);
                    a <<= 1;
                    if (a & 0x8000) {
                        Vec3i16Neg(vec3);
                    }
                    Vec3i16ShiftRight(vec3, detailParam);
                    Vec3i16Add(vec3, vec2, vec2);
                }

                PlanetArcProject(renderContext, rf, workspace, vec2);
                PlanetFeatureAddLineSeg(renderContext, rf, workspace, spokeVec, pt, clip);

                workspace->random = random;
                workspace->detailLevel += 2;
                return;
            }
        }
    }

    u16 c = workspace->prevPt.clip | clip;
    if (c & 0x70) {
        if (workspace->detailLevel > -0x14 && !(c & 0x20)) {
            Vec3i16 vec2;
            Vec3i16Add(spokeVec, workspace->prevPt.pos, vec2);

            u16 a = workspace->random >> 16;
            u16 b = workspace->random & 0xffff;
            workspace->random = workspace->random + (u32) a + ((u32) (b) << 16);

            PlanetArcProject(renderContext, rf, workspace, vec2);
            PlanetFeatureAddLineSeg(renderContext, rf, workspace, spokeVec, pt, clip);

            workspace->random = random;
            workspace->detailLevel += 2;
            return;
        }
    }

    workspace->detailLevel += 2;
    i16 yDir = 1; // clipper param
    if (clip < 0) {
        if (workspace->prevPt.clip < 0) {
            PlanetAddLine(renderContext, workspace, spokeVec, pt, clip);
            return;
        } else {
            Vec3i16Copy(spokeVec, workspace->prevPt.pos);
            workspace->prevPt.clip = clip;
            workspace->prevPt.pt = pt;
        }
    } else {
        if (workspace->prevPt.clip >= 0) {
            Vec3i16Copy(spokeVec, workspace->prevPt.pos);

            workspace->prevPt.clip = clip;

            workspace->prevPt.pt = pt;
            return;
        } else {
            workspace->prevPt.clip = clip;
            spokeVec[0] = workspace->prevPt.pos[0];
            pt = workspace->prevPt.pt;

            yDir = -1;
        }
    }

    // Clipper
    if (pt.y > (SURFACE_HEIGHT - 1)) {
        if (workspace->y2Last >= SURFACE_HEIGHT) {
            if (pt.x >= workspace->xLast) {
                if (pt.x != workspace->xLast || yDir >= 0) {
                    return;
                }
            }
        } else {
            workspace->y2Last = SURFACE_HEIGHT;
        }

        workspace->xLast = pt.x;
        workspace->yDir2 = yDir;
        workspace->yMaxHit++;
        return;
    } else {
        if (pt.x >= SURFACE_WIDTH) {
            if (pt.y < workspace->y2Last) {
                return;
            }
            if (pt.y == workspace->y2Last && yDir > 0) {
                return;
            }
            workspace->y2Last = pt.y;
            workspace->yDir2 = yDir;
            workspace->yMaxHit++;
            return;
        } else if (pt.x >= 0) {
            spokeVec[0] += workspace->q[0];
            if (spokeVec[0] >= 0) {
                if (pt.y < workspace->y2Last) {
                    return;
                }
                if (pt.y == workspace->y2Last && yDir > 0) {
                    return;
                }
                workspace->y2Last = pt.y;
                workspace->yDir2 = yDir;
                workspace->yMaxHit++;
                return;
            }
        }

        pt.y--;
        if (pt.y < 0) {
            return;
        }

        // planet xor
        DrawParamsBodyXor* drawXor = BatchBodyXor(renderContext->zTree);
        drawXor->offset = pt.y;
        drawXor->colour = workspace->colour;
        if (workspace->y1Last > pt.y) {
            return;
        }
        if (workspace->y1Last == pt.y && yDir < 0) {
            return;
        }
        workspace->y1Last = pt.y;
        workspace->y1UpdateDir = yDir;
        workspace->y1UpdateCount++;
    }
}

MINTERNAL void PlanetWriteColourToPalette(RenderContext* renderContext, BodyWorkspace* workspace, u16 srcOffset, u16* destColours) {
    if (workspace->isMonoColour) {
        u16 colIndex = workspace->startColour;
        colIndex = ((colIndex >> 2) & 0xf);
        u16 colour12 = workspace->colours[colIndex];
        u16 palIndex = Palette_GetIndexFor12bitColour(renderContext->palette, colour12);
        for (int i = 0; i < 8; i++) {
            destColours[i] = palIndex;
        }
    } else {
        u16* srcColours = workspace->colours + srcOffset;
        for (int i = 0; i < 8; i++) {
            destColours[i] = Palette_GetIndexFor12bitColour(renderContext->palette, srcColours[i]);
        }
    }
}

MINTERNAL void RenderBody_ReadColours(RenderContext* scene, RenderFrame* rf, BodyWorkspace* bodyWorkspace) {
    u16 colour1 = ByteCodeRead16u(rf);
    u16 colour2 = ByteCodeRead16u(rf);
    u16 colour3 = ByteCodeRead16u(rf);
    u16 colour4 = ByteCodeRead16u(rf);

    // First colour top bits control how we setup planet colours
    u16 colourMode = colour1 >> 12;
    bodyWorkspace->colourMode = colourMode;
    colour1 &= 0xfff;

    if (colourMode & 0x1) {
        u16 colourParamOffset = ByteCodeRead16u(rf);
        colourParamOffset = GetValueForParam8(rf, colourParamOffset);

        if (colourParamOffset) {
            colourParamOffset = colourParamOffset << 2;

            ByteCodeSkipBytes(rf, colourParamOffset);

            colour1 = ByteCodeRead16u(rf);
            colour2 = ByteCodeRead16u(rf);
            colour3 = ByteCodeRead16u(rf);
            colour4 = ByteCodeRead16u(rf);

            ByteCodeSkipBytes(rf, 0x38 - (colourParamOffset + 0x8));
        } else {
            ByteCodeSkipBytes(rf, 0x38);
        }
    }

    if (colourMode & 0x4) {
        bodyWorkspace->colours[0] = colour1;
        bodyWorkspace->colours[1] = colour2;
        bodyWorkspace->colours[2] = colour3;
        bodyWorkspace->colours[3] = colour4;

        colour1 += rf->shadeRamp[4];
        colour2 += rf->shadeRamp[4];
        colour3 += rf->shadeRamp[4];
        colour4 += rf->shadeRamp[4];

        bodyWorkspace->colours[12] = colour1;
        bodyWorkspace->colours[13] = colour2;
        bodyWorkspace->colours[14] = colour3;
        bodyWorkspace->colours[15] = colour4;

        bodyWorkspace->colours[4] = colour1;
        bodyWorkspace->colours[5] = colour2;
        bodyWorkspace->colours[6] = colour3;
        bodyWorkspace->colours[7] = colour4;

        i16 val1 = bodyWorkspace->radiusScaled;

        val1 = (val1 >> (2 - scene->renderDetail2));
        if (val1 <= 0x30) {
            bodyWorkspace->colours[8] = colour1;
            bodyWorkspace->colours[9] = colour2;
            bodyWorkspace->colours[10] = colour3;
            bodyWorkspace->colours[11] = colour4;
        } else {
            u16 colourTint;
            if ((val1 >= 0x60) && (colourMode & 0x2)) {
                colourTint = rf->shadeRamp[5];
                bodyWorkspace->colours[4] = bodyWorkspace->colours[0] + colourTint;
                bodyWorkspace->colours[5] = bodyWorkspace->colours[1] + colourTint;
                bodyWorkspace->colours[6] = bodyWorkspace->colours[2] + colourTint;
                bodyWorkspace->colours[7] = bodyWorkspace->colours[3] + colourTint;
                colourTint = rf->shadeRamp[7];
            } else {
                colourTint = rf->shadeRamp[6];
            }
            bodyWorkspace->colours[8] = bodyWorkspace->colours[0] + colourTint;
            bodyWorkspace->colours[9] = bodyWorkspace->colours[1] + colourTint;
            bodyWorkspace->colours[10] = bodyWorkspace->colours[2] + colourTint;
            bodyWorkspace->colours[11] = bodyWorkspace->colours[3] + colourTint;
        }
    } else {
        bodyWorkspace->colours[0] = colour1;
        bodyWorkspace->colours[1] = colour2;
        bodyWorkspace->colours[2] = colour3;
        bodyWorkspace->colours[3] = colour4;
        if (!(colourMode & 0x1)) {
            colour1 = ByteCodeRead16u(rf);
            colour2 = ByteCodeRead16u(rf);
            colour3 = ByteCodeRead16u(rf);
            colour4 = ByteCodeRead16u(rf);
        }
        bodyWorkspace->colours[4] = colour1;
        bodyWorkspace->colours[5] = colour2;
        bodyWorkspace->colours[6] = colour3;
        bodyWorkspace->colours[7] = colour4;
        if (colourMode & 0x8) {
            u16 colourTint = rf->shadeRamp[4];
            for (int i = 0; i < 8; i++) {
                bodyWorkspace->colours[i + 8] = bodyWorkspace->colours[i] + colourTint;
            }
        }
    }
}

// body/planet render
MINTERNAL int RenderPlanet(RenderContext* renderContext, u16 funcParam) {
    RenderFrame* rf = GetRenderFrame(renderContext);

    BodyWorkspace workspace;

    u16 byteCodeSize = (funcParam >> 4) & 0xffe; // Size of data

    u16 radiusParm = ByteCodeRead16u(rf);
    u16 param2 = ByteCodeRead16u(rf);

    // Save byte code offset
    workspace.initialCodeOffset = rf->byteCodePos;

    workspace.detailLevel = 16 + renderContext->renderDetail2;

    u16 vi = param2 & 0xff;
    VertexData* v = ProjectVertex(renderContext, rf, vi);

    i16 frameScale = rf->scale + 7;

    i16 radius = FloatRebase(&frameScale, radiusParm);

    Vec3i32 centerVec;
    Vec3i32Copy(v->vVec, centerVec);

    u16 radiusScale = Vec3i16ScaleBelow(centerVec, 0x3f00);

    radiusScale = frameScale - radiusScale;

    i32 radiusScaled = FloatScaleUp16s32s(radius, &radiusScale);

    if (!radiusScaled) {
        ByteCodeSkipBytes(rf, byteCodeSize);
        return 0;
    }

    workspace.radiusScale = radiusScale;

    if (radiusScaled > 0x4000) {
        Vec3i32ShiftRight(centerVec, 1);
        workspace.radiusScale++;
    }

    workspace.radiusScaled = radiusScaled;
    workspace.center[0] = centerVec[0];
    workspace.center[1] = centerVec[1];
    workspace.center[2] = centerVec[2];

    if (-radiusScaled >= centerVec[2]) {
        ByteCodeSkipBytes(rf, byteCodeSize);
        return 0;
    }

    i16 ctrlPtOffset = 42 * SCREEN_SCALE;

    workspace.centerX = Float16Make(v->vVec[0]);
    workspace.centerY = Float16Make(v->vVec[1]);
    workspace.centerZ = Float16Make(v->vVec[2]);

    workspace.radius.v = radius;
    workspace.radius.p = frameScale;

    Float32 x2 = Float32Rebase(Float16Mult32(workspace.centerX, workspace.centerX));
    Float32 y2 = Float32Rebase(Float16Mult32(workspace.centerY, workspace.centerY));
    Float32 z2 = Float32Rebase(Float16Mult32(workspace.centerZ, workspace.centerZ));
    Float32 r2 = Float32Rebase(Float16Mult32(workspace.radius, workspace.radius));

    Float32 tangentZ2 = Float32Sub(z2, r2); // tangent len ignoring 2d x,y pos

    workspace.tangentZ2.v = (i16)(tangentZ2.v >> 16);
    workspace.tangentZ2.p = tangentZ2.p;

    Float32 screenDist2 = Float32Add(x2, y2);
    Float32 tangentDepth2 = Float32Add(screenDist2, tangentZ2);

    i16 needCalc = TRUE;
    if (tangentDepth2.v > 0) {
        i16 s = r2.p - tangentDepth2.p;
        workspace.distLog2 = s;
        if (s < 7) {
            needCalc = FALSE;
        } else {
            ctrlPtOffset = 10 * SCREEN_SCALE;
            if (s < 8) {
                needCalc = FALSE;
            }
        }
    }

    if (needCalc) {
        workspace.distLog2 = 0x10;
        ctrlPtOffset = 5 * SCREEN_SCALE;

        Float32 radiusFloat2 = Float32Add(r2, tangentDepth2); // tangent dist

        i32 dv = radiusFloat2.v >> 12;
        radiusFloat2.v -= dv;

        Float16 radiusFloat = Float32Sqrt(radiusFloat2);

        workspace.radius = radiusFloat;

        if (workspace.radiusScale) {
            radiusFloat.v >>= workspace.radiusScale - 16;
        } else {
            radiusFloat.v = 0;
        }
        workspace.radiusScaled = radiusFloat.v;

        tangentZ2 = Float32Sub(z2, radiusFloat2);
        tangentDepth2 = Float32Add(screenDist2, tangentZ2);
    }

    workspace.tangentDepth = Float32Sqrt(tangentDepth2);

    Float16 screenDist = Float32Sqrt(screenDist2);

    if (screenDist.v == 0) {
        workspace.axisX = 0x7fff;
        workspace.axisY = 0;
    } else {
        Float16 axisX = Float16Div(workspace.centerX, screenDist);
        axisX.p += 15;
        workspace.axisX = Float16Extract(axisX).v;

        Float16 axisY = Float16Div(workspace.centerY, screenDist);
        axisY.p += 15;
        workspace.axisY = Float16Extract(axisY).v;
    }

    RenderBody_ReadColours(renderContext, rf, &workspace);

    Float16 centerDist = Float16Mult16(screenDist, workspace.centerZ);

    Float16 radiusDist = Float16Mult16(workspace.radius, workspace.tangentDepth);

    Float16 innerAxisDist = Float16Sub(centerDist, radiusDist);

    Float16 innerAxisDistProjected = Float16Div(innerAxisDist, workspace.tangentZ2);
    innerAxisDistProjected.p += ZSCALE;
    innerAxisDistProjected = Float16Extract(innerAxisDistProjected);

    if (innerAxisDistProjected.v > PLANET_SCREENSPACE_MAX_DIST) {
        // Looking away from (up/out) planet horizon - change background colour to planet sky colour
        u16 baseColour = ByteCodeRead16u(rf);
        workspace.baseColour = baseColour;
        if (baseColour) {
            CalcSkyColour(renderContext, rf, &workspace);
        }
    } else {
        // Normal planet drawing
        Float16 axisXRight = Float16Add(centerDist, radiusDist);
        Float16 outerAxisDistProjected = Float16Div(axisXRight, workspace.tangentZ2);
        outerAxisDistProjected.p += ZSCALE;
        outerAxisDistProjected = Float16Extract(outerAxisDistProjected);

        b32 offCenterDraw = TRUE;
        if (workspace.tangentZ2.v < 0) {
            outerAxisDistProjected.v = PLANET_SCREENSPACE_MAX_DIST2;
        } else {
            // If planet off center draw only half
            i32 centerBy2 = outerAxisDistProjected.v + innerAxisDistProjected.v;

            if (centerBy2 < PLANET_SCREENSPACE_MIN_SCATTER) {
                offCenterDraw = FALSE;
            } else if (centerBy2 > PLANET_SCREENSPACE_MAX_DIST2) {
                outerAxisDistProjected.v = PLANET_SCREENSPACE_MAX_DIST2;
            }
        }

        if (offCenterDraw) {
            // Render off center, optionally render atmosphere / halo
            i32 nearDistScreen = innerAxisDistProjected.v - (3 * SCREEN_SCALE);
            i32 distToCtrlPtCenter = nearDistScreen - ctrlPtOffset;

            i16 ctrlPtOffset3x = ctrlPtOffset * 3;
            i32 distToCenter = nearDistScreen + ctrlPtOffset3x;
            Float16 ctrlPtOffsetScreenSpace = {ctrlPtOffset3x, 18 - ZSCALE};
            ctrlPtOffsetScreenSpace = Float16uRebase(ctrlPtOffsetScreenSpace);

            Float16 q = Float16Div(ctrlPtOffsetScreenSpace, workspace.tangentDepth);

            Float16 r = Float16Mult16(q, workspace.radius);
            r = Float16Sqrt(r);

            Float16 s = Float16Div(q, workspace.radius);

            Float16 u = Float16Mult16(r, s);
            u = Float16Mult16(u, workspace.tangentZ2);
            u.p += ZSCALE - 6;
            u = Float16Extract(u);

            r.p += ZSCALE - 1;
            r = Float16Extract(r);

            i32 majorRadius = (u.v - r.v);
            i32 controlPtsMajor = ((i32)r.v / 3) + u.v;

            i32 outerAxisDist2 = outerAxisDistProjected.v << 1;

            i32 screenSpaceFarX = ((SURFACE_WIDTH << 15) + ((outerAxisDist2 * workspace.axisX) << 1)) >> 16;
            i32 screenSpaceFarY = ((SURFACE_HEIGHT << 15) - ((outerAxisDist2 * workspace.axisY) << 1)) >> 16;

            i32 ptAxisX = (majorRadius * (i32)workspace.axisY) >> 15;
            i32 ptAxisY = (majorRadius * (i32)workspace.axisX) >> 15;
            i32 centerX = (distToCenter * (i32)workspace.axisX) >> 15;
            i32 centerY = (distToCenter * (i32)workspace.axisY) >> 15;

            i32 cptAxisX = (controlPtsMajor * (i32)workspace.axisY) >> 15;
            i32 cptAxisY = (controlPtsMajor * (i32)workspace.axisX) >> 15;
            i32 cptCenterX = (distToCtrlPtCenter * (i32)workspace.axisX) >> 15;
            i32 cptCenterY = (distToCtrlPtCenter * (i32)workspace.axisY) >> 15;

            Vec2i16 bezierPt[4];
            bezierPt[0].x = centerX - ptAxisX;
            bezierPt[0].y = centerY + ptAxisY;

            bezierPt[1].x = cptCenterX + cptAxisX;
            bezierPt[1].y = cptCenterY - cptAxisY;

            bezierPt[2].x = cptCenterX - cptAxisX;
            bezierPt[2].y = cptCenterY + cptAxisY;

            bezierPt[3].x = centerX + ptAxisX;
            bezierPt[3].y = centerY - ptAxisY;

            for (int i = 0; i < 4; i++) {
                bezierPt[i] = ScreenCoords(bezierPt[i]);
            }

            u16 baseColour = ByteCodeRead16u(rf);
            workspace.baseColour = baseColour;
            if (baseColour) {
                if (nearDistScreen > (-0xb2 * SCREEN_SCALE)) {
                    // Draw atmospheric bands
                    i16 bandWidth = ByteCodeRead16i(rf);
                    CalcSkyColour(renderContext, rf, &workspace);

                    Vec2i16 bezierPt2[4];
                    for (int i = 0; i < 4; i++) {
                        bezierPt2[i] = bezierPt[i];
                    }

                    do {
                        if (renderContext->renderPlanetAtmos < bandWidth) {
                            if (!renderContext->currentBatchId) {
                                AddDrawNode(renderContext->zTree, -1, DRAW_FUNC_SPANS_START);
                            } else {
                                BatchSpanStart(renderContext->zTree);
                            }

                            DrawParamsBezier* bezier1 = BatchSpanBezier(renderContext->zTree);
                            DrawParamsBezier* bezier2 = BatchSpanBezier(renderContext->zTree);
                            DrawParamsLine* line1 = BatchSpanLine(renderContext->zTree);
                            DrawParamsLine* line2 = BatchSpanLine(renderContext->zTree);

                            bezier1->pts[0] = bezierPt2[0];
                            bezier1->pts[1] = bezierPt2[1];
                            bezier1->pts[2] = bezierPt2[2];
                            bezier1->pts[3] = bezierPt2[3];

                            line1->x2 = bezierPt2[0].x;
                            line1->y2 = bezierPt2[0].y;
                            line2->x1 = bezierPt2[3].x;
                            line2->y1 = bezierPt2[3].y;

                            for (int i = 0; i < 4; i++) {
                                bezierPt2[i].x = screenSpaceFarX + (((bezierPt2[i].x - screenSpaceFarX) * bandWidth) >> 14);
                                bezierPt2[i].y = screenSpaceFarY + (((bezierPt2[i].y - screenSpaceFarY) * bandWidth) >> 14);
                            }

                            bezier2->pts[0] = bezierPt2[0];
                            bezier2->pts[1] = bezierPt2[1];
                            bezier2->pts[2] = bezierPt2[2];
                            bezier2->pts[3] = bezierPt2[3];

                            line1->x1 = bezierPt2[0].x;
                            line1->y1 = bezierPt2[0].y;
                            line2->x2 = bezierPt2[3].x;
                            line2->y2 = bezierPt2[3].y;

                            u16 colour = ByteCodeRead16u(rf);
                            colour += workspace.lastTint;

                            DrawParamsColour* drawEnd = BatchSpanEnd(renderContext->zTree);
                            drawEnd->colour = Palette_GetIndexFor12bitColour(renderContext->palette, colour);
                        } else {
                            ByteCodeSkipBytes(rf, 2);
                        }
                        bandWidth = ByteCodeRead16u(rf);
                    } while (bandWidth);
                } else {
                    // skip atmosphere colours
                    u16 bandWidth = ByteCodeRead16u(rf);
                    while (bandWidth) {
                        ByteCodeSkipBytes(rf, 2);
                        bandWidth = ByteCodeRead16u(rf);
                    }
                }
            }

            PlanetWriteBezierNode(renderContext, rf, v, bezierPt);

            DrawParamsLineColour* drawLine1 = BatchBodyLine(renderContext->zTree);
            drawLine1->x1 = bezierPt[0].x;
            drawLine1->y1 = bezierPt[0].y;
            drawLine1->x2 = screenSpaceFarX;
            drawLine1->y2 = screenSpaceFarY;
            drawLine1->colour = 0;

            DrawParamsLineColour* drawLine2 = BatchBodyLine(renderContext->zTree);
            drawLine2->x1 = screenSpaceFarX;
            drawLine2->y1 = screenSpaceFarY;
            drawLine2->x2 = bezierPt[3].x;
            drawLine2->y2 = bezierPt[3].y;
            drawLine2->colour = 0;
        } else {
            Float16 nearestPointDepth = Float16Sqrt(workspace.tangentZ2);
            Float16 rDepthProject = Float16Div(workspace.radius, nearestPointDepth);

            Float16 twoThirds = {F16_TWO_THIRDS, 1}; // Constant to move control points to approx ellipse with bezier
            Float16 cpDiameterView = Float16Mult16(rDepthProject, twoThirds);
            cpDiameterView.p += ZSCALE;
            Float16 cpDiameterScreenF = Float16Extract(cpDiameterView);
            i16 cpDiameterScreen = cpDiameterScreenF.v;

            i16 pt0x = (innerAxisDistProjected.v * (i32)workspace.axisX) >> 15;
            i16 pt0y = (innerAxisDistProjected.v * (i32)workspace.axisY) >> 15;
            i16 pt1x = (outerAxisDistProjected.v * (i32)workspace.axisX) >> 15;
            i16 pt1y = (outerAxisDistProjected.v * (i32)workspace.axisY) >> 15;

            i16 cpAxisW = (cpDiameterScreen * (i32)workspace.axisY) >> 15;
            i16 cpAxisH = (cpDiameterScreen * (i32)workspace.axisX) >> 15;

            i16 cpt0x = pt0x - cpAxisW;
            i16 cpt0y = pt0y + cpAxisH;
            i16 cpt1x = pt1x - cpAxisW;
            i16 cpt1y = pt1y + cpAxisH;

            Vec2i16 bezierPt[4];
            bezierPt[0].x = pt0x;
            bezierPt[0].y = pt0y;

            bezierPt[1].x = cpt0x;
            bezierPt[1].y = cpt0y;

            bezierPt[2].x = cpt1x;
            bezierPt[2].y = cpt1y;

            bezierPt[3].x = pt1x;
            bezierPt[3].y = pt1y;

            for (int i = 0; i < 4; i++) {
                bezierPt[i] = ScreenCoords(bezierPt[i]);
            }

            PlanetWriteBezierNode(renderContext, rf, v, bezierPt);

            bezierPt[0].x = pt1x;
            bezierPt[0].y = pt1y;

            bezierPt[1].x = (pt1x << 1) - cpt1x;
            bezierPt[1].y = (pt1y << 1) - cpt1y;

            bezierPt[2].x = (pt0x << 1) - cpt0x;
            bezierPt[2].y = (pt0y << 1) - cpt0y;

            bezierPt[3].x = pt0x;
            bezierPt[3].y = pt0y;

            for (int i = 0; i < 4; i++) {
                bezierPt[i] = ScreenCoords(bezierPt[i]);
            }

            DrawParamsBezierColour* drawBezier = BatchBodyBezier(renderContext->zTree);
            drawBezier->pts[0] = bezierPt[0];
            drawBezier->pts[1] = bezierPt[1];
            drawBezier->pts[2] = bezierPt[2];
            drawBezier->pts[3] = bezierPt[3];
            drawBezier->colour = 0;

            // Skip atmosphere colours
            u16 bandWidth = ByteCodeRead16u(rf);
            if (bandWidth) {
                ByteCodeSkipBytes(rf, 2);
                while (bandWidth) {
                    ByteCodeSkipBytes(rf, 2);
                    bandWidth = ByteCodeRead16u(rf);
                }
            }
        }

        workspace.isMonoColour = 1;
        workspace.startColour = 0;

        // Colour and feature type
        i8 featureCtrl = ByteCodeRead8i(rf);

        if (featureCtrl) {
            workspace.doneRenderFeatures = 0;
            workspace.radiusFeatureDraw = (workspace.radiusScaled * 0x82c0) >> 15; // * 1.02
            workspace.random = (rf->renderContext->modelVars[3] << 16) + rf->renderContext->modelVars[2];

            while (featureCtrl) {
                if (featureCtrl < 0) {
                    // Render circle on sphere (arc with smaller section filled in with given colour)
                    workspace.detailParam = 0;

                    Vec3i16 vec;
                    vec[0] = -((i16)ByteCodeRead8i(rf) << 8);
                    vec[1] = -((i16)ByteCodeRead8i(rf) << 8);
                    vec[2] = -((i16)ByteCodeRead8i(rf) << 8);

                    Vec3i16 vecProjected;
                    MultVec3i16MatrixTranspose(rf->modelViewMatrix, vec, vecProjected);

                    // Get angle between vec and outline of circle - 0 (0x00) smallest - 90 degree (0xff) max
                    u16 angle = ((u16)ByteCodeRead8u(rf) << 6); //  2^6 = 2^(8 - 2), 2^2 = 4, 360 / 4 = 90 degree
                    i16 sine, cosine;
                    LookupSineAndCosine(angle, &sine, &cosine);

                    i16 arcRadius = (i16)(((i32)(workspace.radiusFeatureDraw * sine)) >> 15);
                    if (arcRadius >= 0x64) {
                        i16 arcOffset = (i16)(((i32)(workspace.radiusFeatureDraw * cosine)) >> 15);
                        workspace.colour = featureCtrl & 0x1c;
                        PlanetCircle(renderContext, rf, &workspace, vecProjected, arcRadius, arcOffset);
                        if (featureCtrl & 0x40) {
                            // Mirror circle on other size of sphere
                            Vec3i16Neg(vecProjected);
                            PlanetCircle(renderContext, rf, &workspace, vecProjected, arcRadius, arcOffset);
                        }
                    }
                } else {
                    // Render complex poly on sphere
                    i16 arcStarted = 0;
                    workspace.outsideSphere = 0;
                    workspace.colour = featureCtrl;
                    workspace.arcRadius = workspace.radiusScaled;
                    Vec3i16Zero(workspace.q);
                    i16 detailLevel = ByteCodeRead8i(rf);
                    if (detailLevel) {
                        workspace.detailParam = (detailLevel + workspace.radiusScale + 1) << 1;
                    } else {
                        workspace.detailParam = 0;
                    }

                    i8 paramX = ByteCodeRead8i(rf);
                    do {
                        Vec3i16 vec;
                        Vec3i16 vecProjected;

                        vec[0] = ((i16) paramX) << 8;
                        vec[1] = ((i16) ByteCodeRead8i(rf)) << 8;
                        vec[2] = ((i16) ByteCodeRead8i(rf)) << 8;

                        MultVec3i16MatrixTranspose(rf->modelViewMatrix, vec, vecProjected);

                        if (arcStarted) {
                            paramX = ByteCodeRead8i(rf);
                            PlanetArcProject(renderContext, rf, &workspace, vecProjected);
                            if (!paramX) {
                                PlanetArcEnd(renderContext, rf, &workspace);
                                goto doneArc;
                            }
                        } else {
                            paramX = ByteCodeRead8i(rf);
                            if (paramX == 0) {
                                // Special check to control inside-out-ness
                                i16 x = ((i16) ByteCodeRead8i(rf)) << 8;
                                BodyPoint bodyPoint;
                                PlanetProjectPoint(&workspace, vecProjected, &bodyPoint);
                                if (workspace.radiusScaled >= 0x1000) {
                                    i32 r = workspace.radiusFeatureDraw * (i32) x;
                                    r = r >> 15;
                                    Vec3i16 vec2;
                                    Vec3i16Add(workspace.center, bodyPoint.pos, vec2);
                                    if (vec2[0] < 0) {
                                        vec2[0] = -vec2[0];
                                    }
                                    if (vec2[1] < 0) {
                                        vec2[1] = -vec2[1];
                                    }
                                    if (vec2[2] < 0) {
                                        vec2[2] = -vec2[2];
                                    }
                                    u32 d2 = vec[0] + vec[1] + vec[2];
                                    if (!FMath_RangedCheck(r, d2)) {
                                        workspace.outsideSphere = -1;
                                    }
                                }
                                paramX = ByteCodeRead8i(rf);
                            } else {
                                PlanetArcStart(&workspace, vecProjected);
                                arcStarted = 1;
                            }
                        }
                    } while (paramX);

                    PlanetArcEnd(renderContext, rf, &workspace);
                }
                doneArc:
                featureCtrl = ByteCodeRead8i(rf);
            }
        }

        workspace.detailParam = 0;
        workspace.doneRenderFeatures = -1;

        if (workspace.colourMode & 0x8) {
            Vec3i16 vec;
            vec[0] = rf->lightV[0];
            vec[1] = rf->lightV[1];
            vec[2] = rf->lightV[2];

            i16 clip = workspace.radiusScaled;
            workspace.colour = 0x20;
            PlanetCircle2(renderContext, rf, &workspace, vec, clip, 0);

            if (workspace.colourMode & 0x4) {
                i32 c2 = clip * (i32)0x7bef;
                c2 >>= 15;
                if (workspace.colourMode & 0x2) {
                    workspace.colour = 0x20;
                    vec[0] = rf->lightV[0];
                    vec[1] = rf->lightV[1];
                    vec[2] = rf->lightV[2];
                    PlanetCircle2(renderContext, rf, &workspace, vec, c2, clip >> 2);
                }

                c2 = clip * (i32)0x7efe;
                c2 >>= 15;

                workspace.colour = 0x30;
                vec[0] = rf->lightV[0];
                vec[1] = rf->lightV[1];
                vec[2] = rf->lightV[2];
                PlanetCircle2(renderContext, rf, &workspace, vec, c2, clip >> 3);
            }
        }

        if (workspace.startColour) {
            DrawParamsBodyXor* bodyXor = BatchBodyXor(renderContext->zTree);
            bodyXor->offset = SURFACE_HEIGHT - 1;
            bodyXor->colour = workspace.startColour;
        }

        if (workspace.colourMode & 0x8) {
            DrawParamsColour16* drawParams = BatchBodyDraw2(renderContext->zTree);
            PlanetWriteColourToPalette(renderContext, &workspace, 0, drawParams->colour);
            PlanetWriteColourToPalette(renderContext, &workspace, 8, drawParams->colour + 8);
        } else {
            DrawParamsColour8* drawParams = BatchBodyDraw3(renderContext->zTree);
            PlanetWriteColourToPalette(renderContext, &workspace, 0, drawParams->colour);
        }
    }

    rf->byteCodePos = workspace.initialCodeOffset;
    ByteCodeSkipBytes(rf, byteCodeSize);

    return 0;
}

MINTERNAL void FrameRenderObjects(RenderContext* rc, ModelData* model) {
    RenderFrame* rf = GetRenderFrame(rc);

    rf->vertexData = (u16*) (((u8*)model) + model->vertexDataOffset);

    rf->numVertexModel = model->verticesDataSize / sizeof(VertexData);
    rf->numVertexTmp = 6;
    rf->numNormals = model->normalDataSize >> (u16)1;
    if (rf->numNormals < 2) {
        rf->numNormals = 2;
    }

    rf->modelData = model;
    rf->normalOffset = model->normalsOffset;

    // Update viewing angle transforms
    int shift = rf->scale;
    shift -= (8 + rf->objViewVecScale);
    if (shift > 0) {
        rf->objViewVecScale += shift;
        Vec3i16ShiftRight(rf->viewM, shift);
    }

#if FRAME_MEM_USE_STACK_ALLOC
    rf->vertexTrans = (VertexData*)alloca(sizeof(VertexData) * (rf->numVertexTmp + rf->numVertexModel));
#elif FRAME_MEM_USE_STACK_DARY
    VertexData vData[rf->numVertexTmp + rf->numVertexModel];
    rf->vertexTrans = vData;
#elif FRAME_MEM_USE_MALLOC
    u8* stackMemStart = rc->memStack->pos;
    rf->vertexTrans = (VertexData*)MMemStackAlloc(rc->memStack, sizeof(VertexData) * (rf->numVertexTmp + rf->numVertexModel));
#endif

    for (int i = 0; i < rf->numVertexTmp + rf->numVertexModel; i++) {
        VertexData* vertex = rf->vertexTrans + i;
        vertex->projectedState = 0;
    }

    // Skip past the tmp vertices
    rf->vertexTrans += rf->numVertexTmp;

#if FRAME_MEM_USE_STACK_DARY
    u16 normals[rf->numNormals];
#endif
    if (rf->numNormals > 0) {
#if FRAME_MEM_USE_STACK_ALLOC
        rf->normals = (u16*)alloca(rf->numNormals * sizeof(u16));
#elif FRAME_MEM_USE_STACK_DARY
        rf->normals = normals;
#elif FRAME_MEM_USE_MALLOC
        rf->normals = (u16*)MMemStackAlloc(rc->memStack, rf->numNormals * sizeof(u16));
#endif
        rf->normals[0] = rf->shadeRamp[3];
        rf->normals[1] = rf->shadeRamp[4];
        for (int i = 2; i < rf->numNormals; i++) {
            rf->normals[i] = NORMAL_NOT_CALCULATED;
        }
    } else {
        rf->normals = NULL;
    }

    rf->frameId = 2;

    u8* data = ((u8*)model) + model->codeOffset;
    rf->byteCodePos = data;
#ifdef FINSPECTOR
    rf->fileDataStartAddress = rf->renderContext->modelDataFileStartAddress;
#endif
    InterpretModelCode(rc, rf);

#if FRAME_MEM_USE_MALLOC
    MMemStackReset(rc->memStack, stackMemStart);
#endif

    rf->vertexTrans = NULL;
    rf->normals = NULL;
}

void Render_Init(SceneSetup* sceneSetup, RasterContext* raster) {
    sceneSetup->raster = raster;
    sceneSetup->random1 = 0x12345678;
    sceneSetup->random2 = 0x89abcdef;
    MMemStackInit(&sceneSetup->memStack, 0x4000);
}

void Render_Free(SceneSetup* sceneSetup) {
    MMemStackFree(&sceneSetup->memStack);
}

void Render_RenderScene(SceneSetup* sceneSetup) {
    RenderContext renderContext;

    memset(renderContext.renderFrame, 0, sizeof(renderContext.renderFrame));

    renderContext.currentRenderFrame = renderContext.renderFrame;
    renderContext.currentRenderFrameIx = 0;

    renderContext.random1 = sceneSetup->random1;
    renderContext.random2 = sceneSetup->random2;

    renderContext.zTree = &sceneSetup->raster->zTree;
    ZTree_Clear(renderContext.zTree);

    renderContext.width = sceneSetup->raster->surface->width;
    renderContext.height = sceneSetup->raster->surface->height;

    renderContext.renderDetail1 = 1;
    renderContext.renderDetail2 = 2;

    renderContext.currentBatchId = 0;

    renderContext.palette = &(sceneSetup->raster->paletteContext);

    renderContext.currentRenderFrame = NULL;
    RenderFrame* rf = PushRenderFrame(&renderContext);

    renderContext.sceneSetup = sceneSetup;
    renderContext.memStack = &sceneSetup->memStack;

    renderContext.renderPlanetAtmos = sceneSetup->renderPlanetAtmos;

    rf->renderContext = sceneSetup;
    rf->matrixWinding = 0;

    ModelData* modelData = Render_GetModel(sceneSetup, sceneSetup->modelIndex);

#ifdef FINSPECTOR
    renderContext.logLevel = sceneSetup->logLevel;
    renderContext.loadedModelIndexes = &sceneSetup->loadedModelIndexes;
    renderContext.byteCodeTrace = &sceneSetup->byteCodeTrace;
#endif

    // Copy rotation to view space
    Matrix3i16Copy(rf->renderContext->viewMatrix, rf->modelViewMatrix);

    rf->x12 = sceneSetup->x12; // scale?

    Vec3i32Copy(sceneSetup->viewVec, rf->objViewVec);

    rf->scale = modelData->scale1;
    rf->zScale = modelData->scale2;

    if (!CheckClipped(&renderContext, modelData)) {
        return;
    }

    rf->tmpVariable[0] = 0;

    rf->baseColour = 0x111;

    // Light vector
    Vec3i16Copy(sceneSetup->lightVec, rf->lightV);

    // Copy light colour ramp
    for (int i = 0; i < 8; i++) {
        rf->shadeRamp[i] = sceneSetup->shadeRamp[i];
    }

    TransformLightAndViewVectors(&renderContext);

    FrameRenderObjects(&renderContext, modelData);

    if (renderContext.currentBatchId != 0) {
        WriteDrawFunc(renderContext.zTree, DRAW_FUNC_BATCH_END);
    }
    renderContext.currentBatchId = 0;

    // Save updated seed for next frame
    sceneSetup->random1 = renderContext.random1;
    sceneSetup->random2 = renderContext.random2;
}

MINTERNAL void InterpretModelCode(RenderContext* renderContext, RenderFrame* rf) {
    // Call render funcs until hit end byte code, or draw buffer is full
    while (!ByteCodeIsDone(rf)) {
#ifdef FINSPECTOR
        u32 byteCodeOffset = rf->byteCodePos - rf->fileDataStartAddress;
        if (renderContext->logLevel) {
            ByteCodeTrace trace;
            trace.index = byteCodeOffset;
            trace.result = 0;
            MArrayAdd(*(renderContext->byteCodeTrace), trace);
        }
        renderContext->zTree->insOffsetTmp = byteCodeOffset;
#endif
        u16 funcParam = ByteCodeRead16u(rf);
        u8 funcOffset = (funcParam & (u16)0x1f);
        int ret = 0;

        switch (funcOffset) {
            case Render_DONE:
                ret = -1;
                break;
            case Render_CIRCLE: {
                ret = RenderCircle(renderContext, funcParam);
                break;
            }
            case Render_LINE: {
                ret = RenderLine(renderContext, funcParam);
                break;
            }
            case Render_TRI: {
                ret = RenderTri(renderContext, funcParam);
                break;
            }
            case Render_QUAD: {
                ret = RenderQuad(renderContext, funcParam);
                break;
            }
            case Render_COMPLEX: {
                ret = RenderComplex(renderContext, funcParam);
                break;
            }
            case Render_BATCH: {
                ret = RenderBatch(renderContext, funcParam);
                break;
            }
            case Render_MIRRORED_TRI: {
                ret = Render2TriMirrored(renderContext, funcParam);
                break;
            }
            case Render_MIRRORED_QUAD: {
                ret = Render2QuadMirrored(renderContext, funcParam);
                break;
            }
            case Render_TEARDROP: {
                ret = RenderTeardrop(renderContext, funcParam);
                break;
            }
            case Render_VTEXT: {
                ret = RenderVectorText(renderContext, funcParam);
                break;
            }
            case Render_IF: {
                ret = RenderIf(renderContext, funcParam);
                break;
            }
            case Render_IF_NOT: {
                ret = RenderIfNot(renderContext, funcParam);
                break;
            }
            case Render_CALC_A:
            case Render_CALC_B: {
                ret = RenderCalc(renderContext, funcParam);
                break;
            }
            case Render_MODEL: {
                ret = RenderModel(renderContext, funcParam);
                break;
            }
            case Render_AUDIO_CUE: {
                ret = RenderAudioCue(renderContext, funcParam);
                break;
            }
            case Render_CONE: {
                ret = RenderCone(renderContext, funcParam);
                break;
            }
            case Render_CONE_COLOUR_CAP: {
                ret = RenderConeCapped(renderContext, funcParam);
                break;
            }
            case Render_BITMAP_TEXT: {
                MLog("bitmap text not done!");
                ret = 0;
                break;
            }
            case Render_IF_NOT_VAR: {
                ret = RenderIfVar(renderContext, funcParam);
                break;
            }
            case Render_IF_VAR: {
                ret = RenderIfNotVar(renderContext, funcParam);
                break;
            }
            case Render_CLEARZ: {
                ret = RenderClearZ(renderContext, funcParam);
                break;
            }
            case Render_LINE_BEZIER: {
                ret = RenderLineBezier(renderContext, funcParam);
                break;
            }
            case Render_IF_VIEWSPACE_DIST: {
                ret = RenderViewspaceDist(renderContext, funcParam);
                break;
            }
            case Render_BALLS: {
                ret = RenderBalls(renderContext, funcParam);
                break;
            }
            case Render_MATRIX_SETUP: {
                ret = RenderMatrixSetup(renderContext, funcParam);
                break;
            }
            case Render_COLOUR: {
                ret = RenderColour(renderContext, funcParam);
                break;
            }
            case Render_MODEL_SCALE: {
                ret = RenderModelScale(renderContext, funcParam);
                break;
            }
            case Render_MATRIX_TRANSFORM: {
                ret = RenderMatrixTransform(renderContext, funcParam);
                break;
            }
            case Render_MATRIX_COPY: {
                ret = RenderMatrixCopy(renderContext, funcParam);
                break;
            }
            case Render_PLANET: {
                ret = RenderPlanet(renderContext, funcParam);
                break;
            }
            default:
                ret = -1;
                break;
        }

        if (ret < 0) {
            break;
        }
    }
}

void Render_RenderAndDrawScene(SceneSetup* sceneSetup, b32 resetPalette) {
#ifdef FINSPECTOR
    MArrayClear(sceneSetup->loadedModelIndexes);
    MArrayClear(sceneSetup->byteCodeTrace);
#endif

    Palette_SetupForNewFrame(&sceneSetup->raster->paletteContext, resetPalette);
    Render_RenderScene(sceneSetup);
    Palette_CalcDynamicColourUpdates(&sceneSetup->raster->paletteContext);
    Surface_Clear(sceneSetup->raster->surface, BACKGROUND_COLOUR_INDEX);
    Raster_Draw(sceneSetup->raster);
}

void testClipping(void) {
    Vec2i16 p1;
    p1.x = 240;
    p1.y = 67;

    Vec2i16 p2;
    p2.x = -4443;
    p2.y = 1898;

    b32 shown = ClipLinePoints(&p1, &p2, 320 - 1, 0xa8 - 1);
    if (!shown || p1.x < 0 || p2.x < 0 || p1.x >= 320 || p2.x >= 320) {
        MLog("Error");
    }

    p1.x = 4;
    p1.y = -54;
    p2.x = -37;
    p2.y = 27;

    shown = ClipLinePoints(&p1, &p2, 320 - 1, 0xa8 - 1);
    if (shown) {
        MLog("Error");
    }

    VertexData v1;
    v1.vVec[0] = -9674271;
    v1.vVec[1] = -177148;
    v1.vVec[2] = 4588555;

    VertexData v2;
    v2.vVec[0] = -10413839;
    v2.vVec[1] = -177148;
    v2.vVec[2] = -7369901;

//    Vec2i16 pos = ClipLineZ(&v1, &v2, 640, 336);
}

void Render_RunTests() {
    testClipping();
    FMath_Test();
}
