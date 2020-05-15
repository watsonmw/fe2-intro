#include "builddata.h"
#include "mlib.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

i32 Build_Stars(const char* filepath, u8* out, i32 images, i32 tileSize, i32 tileSize2) {
    int w = 0;
    int h = 0;
    int channels = 0;
    stbi_uc* result = stbi_load(filepath, &w, &h, &channels, 4);

    if (!result) {
        MLogf("Failed to load image file '%s' : '%s'", filepath, stbi_failure_reason());
        return 0;
    }

    i16 x = 1;
    i16 y = 1;

    i32 written = 0;

    stbi_uc* src = result;

    u16 sizes1[32];
    u16 sizes2[32];
    u32 colours[10];
    u32 colourEncountered = 0;

    for (int i = 0; i < images; i ++) {
        u32 offset = 0;
        b32 found = FALSE;
        for (int yy = 0; yy < tileSize; yy++) {
            u8* srcLine = src + (((yy + y) * w + x) * 4);
            i16 start1 = 0;
            i16 start2 = 0;
            for (int xx = 0; xx < tileSize; xx++) {
                u8 r = (*srcLine++);
                u8 g = (*srcLine++);
                u8 b = (*srcLine++);
                u8 a = (*srcLine++);

                b32 empty = (a == 0) || (r == 0 && b == 0 && g == 0);
                u32 colourIndex = -1;
                if (!empty) {
                    u32 colour = (u32)r << 16 | (u32)b << 8 | g;
                    for (int j = 0; j < colourEncountered; ++j) {
                        if (colours[j] == colour) {
                            colourIndex = j;
                            break;
                        }
                    }
                    if (colourIndex == -1) {
                        colourIndex = colourEncountered;
                        colours[colourEncountered++] = colour;
                    }
                }

                i32 pos = tileSize - xx;
                if (colourIndex == 0) {
                    if (pos > start1) {
                        start1 = pos;
                    }
                } else if (colourIndex == 1) {
                    if (pos > start2) {
                        start2 = pos;
                    }
                }
            }
            sizes1[offset] = start1;
            sizes2[offset++] = start2;

            if (start1 || start2) {
                found = TRUE;
            }
        }

        if (found) {
            MLogf("    // %d", i);

            MLogfNoNewLine("   ");
            for (int j = 0; j < tileSize; ++j) {
                MLogfNoNewLine(" %d,", sizes1[j]);
                if (out) {
                    *(out++) = sizes1[j];
                }
            }

            MLogfNoNewLine("\n   ");
            for (int j = 0; j < tileSize; ++j) {
                MLogfNoNewLine(" %d,", sizes2[j]);
                if (out) {
                    *(out++) = sizes2[j];
                }
            }

            MLogf("");
            written++;
        }

        x += tileSize2 + 1;
        if (x > 320 - (tileSize2 + 1)) {
            x = 1;
            y += tileSize2 + 1;
        }
    }

    stbi_image_free(result);

    return written;
}

